/****************************************************************************
 * app/p4x_selftest/p4x_camera_capture.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "p4x_camera_capture.h"

#ifdef CONFIG_VIDEO
#  include <sys/videoio.h>
#endif

#define P4X_CAMERA_CAPTURE_TIMEOUT_MS 5000
#define P4X_CAMERA_CAPTURE_ALIGN       32

#ifndef CONFIG_VIDEO
int p4x_camera_capture_one(const char *device, const char *output,
                           uint16_t width, uint16_t height, uint16_t fps,
                           uint32_t *bytesused)
{
  (void)device;
  (void)output;
  (void)width;
  (void)height;
  (void)fps;
  (void)bytesused;
  return -ENOSYS;
}
#else
static int p4x_camera_write_frame(const char *path, const void *buffer,
                                  size_t length)
{
  char block[256];
  int fd;
  ssize_t ret;
  size_t offset = 0;

  if (path == NULL || path[0] == '\0')
    {
      return 0;
    }

  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    {
      return -errno;
    }

  while (offset < length)
    {
      size_t chunk = length - offset;
      if (chunk > sizeof(block))
        {
          chunk = sizeof(block);
        }

      memcpy(block, (const uint8_t *)buffer + offset, chunk);
      ret = write(fd, block, chunk);
      if (ret <= 0)
        {
          int error = errno > 0 ? errno : EIO;
          close(fd);
          return -error;
        }

      offset += ret;
    }

  if (close(fd) < 0)
    {
      return -errno;
    }

  return 0;
}

int p4x_camera_capture_one(const char *device, const char *output,
                           uint16_t width, uint16_t height, uint16_t fps,
                           uint32_t *bytesused)
{
  struct v4l2_format format;
  struct v4l2_requestbuffers request;
  struct v4l2_streamparm streamparm;
  struct v4l2_buffer buffer;
  struct pollfd pollfd;
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  uint32_t expected_size;
  void *frame = NULL;
  int fd = -1;
  int ret;
  int error = EIO;

  if (device == NULL || width == 0 || height == 0 || fps == 0)
    {
      return -EINVAL;
    }

  expected_size = (uint32_t)width * height;
  frame = memalign(P4X_CAMERA_CAPTURE_ALIGN, expected_size);
  if (frame == NULL)
    {
      return -ENOMEM;
    }

  fd = open(device, O_RDWR);
  if (fd < 0)
    {
      error = errno > 0 ? errno : ENODEV;
      goto errout;
    }

  memset(&format, 0, sizeof(format));
  format.type = type;
  format.fmt.pix.width = width;
  format.fmt.pix.height = height;
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_SBGGR8;
  format.fmt.pix.field = V4L2_FIELD_NONE;

  ret = ioctl(fd, VIDIOC_S_FMT, (uintptr_t)&format);
  if (ret < 0)
    {
      error = errno > 0 ? errno : EIO;
      goto errout;
    }

  /* Do not silently accept a different mode negotiated by the driver. */
  if (format.fmt.pix.width != width || format.fmt.pix.height != height ||
      format.fmt.pix.pixelformat != V4L2_PIX_FMT_SBGGR8 ||
      format.fmt.pix.sizeimage < expected_size)
    {
      error = EPROTO;
      goto errout;
    }

  memset(&streamparm, 0, sizeof(streamparm));
  streamparm.type = type;
  streamparm.parm.capture.timeperframe.numerator = 1;
  streamparm.parm.capture.timeperframe.denominator = fps;
  ret = ioctl(fd, VIDIOC_S_PARM, (uintptr_t)&streamparm);
  if (ret < 0 || streamparm.parm.capture.timeperframe.numerator != 1 ||
      streamparm.parm.capture.timeperframe.denominator != fps)
    {
      error = ret < 0 && errno > 0 ? errno : EPROTO;
      goto errout;
    }

  memset(&request, 0, sizeof(request));
  request.count = 1;
  request.type = type;
  request.memory = V4L2_MEMORY_USERPTR;

  ret = ioctl(fd, VIDIOC_REQBUFS, (uintptr_t)&request);
  if (ret < 0 || request.count < 1)
    {
      error = ret < 0 && errno > 0 ? errno : ENOTSUP;
      goto errout;
    }

  memset(&buffer, 0, sizeof(buffer));
  buffer.type = type;
  buffer.memory = V4L2_MEMORY_USERPTR;
  buffer.index = 0;
  buffer.m.userptr = (uintptr_t)frame;
  buffer.length = expected_size;

  ret = ioctl(fd, VIDIOC_QBUF, (uintptr_t)&buffer);
  if (ret < 0)
    {
      error = errno > 0 ? errno : EIO;
      goto errout;
    }

  ret = ioctl(fd, VIDIOC_STREAMON, (uintptr_t)&type);
  if (ret < 0)
    {
      error = errno > 0 ? errno : EIO;
      goto errout;
    }

  pollfd.fd = fd;
  pollfd.events = POLLIN;
  pollfd.revents = 0;
  ret = poll(&pollfd, 1, P4X_CAMERA_CAPTURE_TIMEOUT_MS);
  if (ret <= 0 || (pollfd.revents & (POLLERR | POLLHUP)) != 0)
    {
      error = ret == 0 ? ETIMEDOUT : (errno > 0 ? errno : EIO);
      goto streamoff;
    }

  memset(&buffer, 0, sizeof(buffer));
  buffer.type = type;
  buffer.memory = V4L2_MEMORY_USERPTR;
  ret = ioctl(fd, VIDIOC_DQBUF, (uintptr_t)&buffer);
  if (ret < 0 || buffer.bytesused < expected_size)
    {
      error = ret < 0 && errno > 0 ? errno : EIO;
      goto streamoff;
    }

  ret = p4x_camera_write_frame(output, frame, buffer.bytesused);
  if (ret < 0)
    {
      error = -ret;
      goto streamoff;
    }

  if (bytesused != NULL)
    {
      *bytesused = buffer.bytesused;
    }

  error = 0;

streamoff:
  ioctl(fd, VIDIOC_STREAMOFF, (uintptr_t)&type);
errout:
  if (fd >= 0)
    {
      close(fd);
    }

  free(frame);
  return error == 0 ? 0 : -error;
}
#endif
