// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "tools/rbd/ArgumentTypes.h"
#include "tools/rbd/Shell.h"
#include "tools/rbd/Utils.h"
#include "common/errno.h"
#include "common/ceph_mutex.h"
#include "common/config_proxy.h"
#include "global/global_context.h"
#include <iostream>
#include <boost/program_options.hpp>

namespace rbd {
namespace action {
namespace resize {

namespace at = argument_types;
namespace po = boost::program_options;

static int do_resize(librbd::Image& image, uint64_t size, bool allow_shrink, bool no_progress)
{
  utils::ProgressContext pc("Resizing image", no_progress);
  int r = image.resize2(size, allow_shrink, pc);
  if (r < 0) {
    pc.fail();
    return r;
  }
  pc.finish();
  return 0;
}

void get_arguments(po::options_description *positional,
                   po::options_description *options) {
  at::add_image_spec_options(positional, options, at::ARGUMENT_MODIFIER_NONE);
  at::add_size_option(options);
  options->add_options()
    ("allow-shrink", po::bool_switch(), "permit shrinking");
  options->add_options()
   (at::IMAGE_THICK_PROVISION.c_str(), po::bool_switch(), "fully allocate storage and zero image");
  at::add_no_progress_option(options);
  at::add_encryption_options(options);
}

void thick_provision_writer_completion(rbd_completion_t, void *);
struct thick_provision_writer {
  librbd::Image *image;
  ceph::mutex lock = ceph::make_mutex("thick_provision_writer::lock");
  ceph::condition_variable cond;
  uint64_t chunk_size;
  uint64_t concurr;
  struct {
    uint64_t in_flight;
    int io_error;
  } io_status;

  // Constructor
  explicit thick_provision_writer(librbd::Image *i, librbd::ImageOptions &o)
    : image(i)
  {
    // If error cases occur, the code is aborted, because
    // constructor cannot return error value.
    ceph_assert(g_ceph_context != nullptr);

    librbd::image_info_t info;
    int r = image->stat(info, sizeof(info));
    ceph_assert(r >= 0);

    uint64_t order = info.order;
    if (order == 0) {
      order = g_conf().get_val<uint64_t>("rbd_default_order");
    }

    auto stripe_count = std::max<uint64_t>(1U, image->get_stripe_count());
    chunk_size = (1ull << order) * stripe_count;

    concurr = std::max<uint64_t>(
      1U, g_conf().get_val<uint64_t>("rbd_concurrent_management_ops") /
            stripe_count);

    io_status.in_flight = 0;
    io_status.io_error = 0;
  }

  int start_io(uint64_t write_offset)
  {
    {
      std::lock_guard l{lock};
      io_status.in_flight++;
      if (io_status.in_flight > concurr) {
        io_status.in_flight--;
        return -EINVAL;
      }
    }

    librbd::RBD::AioCompletion *c;
    c = new librbd::RBD::AioCompletion(this, thick_provision_writer_completion);
    int r;
    r = image->aio_write_zeroes(write_offset, chunk_size, c,
                                RBD_WRITE_ZEROES_FLAG_THICK_PROVISION,
                                LIBRADOS_OP_FLAG_FADVISE_SEQUENTIAL);
    if (r < 0) {
      std::lock_guard l{lock};
      io_status.io_error = r;
    }
    return r;
  }

  int wait_for(uint64_t max) {
    using namespace std::chrono_literals;
    std::unique_lock l{lock};
    int r = io_status.io_error;

    while (io_status.in_flight > max) {
      cond.wait_for(l, 200ms);
    }
    return r;
  }
};

void thick_provision_writer_completion(rbd_completion_t rc, void *pc) {
  librbd::RBD::AioCompletion *ac = (librbd::RBD::AioCompletion *)rc;
  thick_provision_writer *tc = static_cast<thick_provision_writer *>(pc);

  int r = ac->get_return_value();
  tc->lock.lock();
  if (r < 0 &&  tc->io_status.io_error >= 0) {
    tc->io_status.io_error = r;
  }
  tc->io_status.in_flight--;
  tc->cond.notify_all();
  tc->lock.unlock();
  ac->release();
}

int write_data(librbd::Image &image, librbd::ImageOptions &opts,
               bool no_progress) {
  uint64_t image_size;
  int r = 0;
  utils::ProgressContext pc("Thick provisioning", no_progress);

  if (image.size(&image_size) != 0) {
    return -EINVAL;
  }

  thick_provision_writer tpw(&image, opts);
  uint64_t off;
  uint64_t i;
  for (off = 0; off < image_size;) {
    i = 0;
    while (i < tpw.concurr && off < image_size) {
      tpw.wait_for(tpw.concurr - 1);
      r = tpw.start_io(off);
      if (r != 0) {
        goto err_writesame;
      }
      ++i;
      off += tpw.chunk_size;
      if(off > image_size) {
        off = image_size;
      }
      pc.update_progress(off, image_size);
    }
  }

  tpw.wait_for(0);
  r = image.flush();
  if (r < 0) {
    std::cerr << "rbd: failed to flush at the end: " << cpp_strerror(r)
              << std::endl;
    goto err_writesame;
  }
  pc.finish();

  return r;

err_writesame:
  tpw.wait_for(0);
  pc.fail();

  return r;
}

int thick_write(const std::string &image_name,librados::IoCtx &io_ctx,
                librbd::ImageOptions &opts, bool no_progress) {
  int r;
  librbd::Image image;

  r = utils::open_image(io_ctx, image_name, false, &image);
  if (r < 0) {
    return r;
  }

  r = write_data(image, opts, no_progress);

  image.close();

  return r;
}

int execute(const po::variables_map &vm,
            const std::vector<std::string> &ceph_global_init_args) {
  size_t arg_index = 0;
  std::string pool_name;
  std::string namespace_name;
  std::string image_name;
  std::string snap_name;
  int r = utils::get_pool_image_snapshot_names(
    vm, at::ARGUMENT_MODIFIER_NONE, &arg_index, &pool_name, &namespace_name,
    &image_name, &snap_name, true, utils::SNAPSHOT_PRESENCE_NONE,
    utils::SPEC_VALIDATION_NONE);
  if (r < 0) {
    return r;
  }

  librbd::ImageOptions opts;
  r = utils::get_image_options(vm, true, &opts);
  if (r < 0) {
    return r;
  }

  uint64_t size;
  r = utils::get_image_size(vm, &size);
  if (r < 0) {
    return r;
  }

  utils::EncryptionOptions encryption_options;
  r = utils::get_encryption_options(vm, &encryption_options);
  if (r < 0) {
    return r;
  }

  librados::Rados rados;
  librados::IoCtx io_ctx;
  librbd::Image image;
  r = utils::init_and_open_image(pool_name, namespace_name, image_name, "",
                                 snap_name, false, &rados, &io_ctx, &image);
  if (r < 0) {
    return r;
  }

  if (!encryption_options.specs.empty()) {
    r = image.encryption_load2(encryption_options.specs.data(),
                               encryption_options.specs.size());
    if (r < 0) {
      std::cerr << "rbd: encryption load failed: " << cpp_strerror(r)
                << std::endl;
      return r;
    }
  }

  librbd::image_info_t info;
  r = image.stat(info, sizeof(info));
  if (r < 0) {
    std::cerr << "rbd: resize error: " << cpp_strerror(r) << std::endl;
    return r;
  }

  if (info.size == size) {
    std::cerr << "rbd: new size is equal to original size " << std::endl;
    return -EINVAL;
  }

  if (info.size > size && !vm["allow-shrink"].as<bool>()) {
    r = -EINVAL;
  } else {
    r = do_resize(image, size, vm["allow-shrink"].as<bool>(), vm[at::NO_PROGRESS].as<bool>());
  }

  if (r < 0) {
    if (r == -EINVAL && !vm["allow-shrink"].as<bool>()) {
      std::cerr << "rbd: shrinking an image is only allowed with the "
                << "--allow-shrink flag" << std::endl;
      return r;
    }
    std::cerr << "rbd: resize error: " << cpp_strerror(r) << std::endl;
    return r;
  }

  if (vm.count(at::IMAGE_THICK_PROVISION) && vm[at::IMAGE_THICK_PROVISION].as<bool>()) {
    r = thick_write(image_name, io_ctx, opts, vm[at::NO_PROGRESS].as<bool>());
    if (r < 0) {
      std::cerr << "rbd: image created but error encountered during thick provisioning: "
                << cpp_strerror(r) << std::endl;
      return r;
    }
  }

  return 0;
}

Shell::SwitchArguments switched_arguments({"allow-shrink"});
Shell::Action action(
  {"resize"}, {}, "Resize (expand or shrink) image.", "", &get_arguments,
  &execute);

} // namespace resize
} // namespace action
} // namespace rbd
