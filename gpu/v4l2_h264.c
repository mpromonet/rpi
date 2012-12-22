/*
 *  V4L2 video capture example
 *
 *  This program can be used and distributed without restrictions.
 *
 *      This program is provided with the V4L2 API
 * see http://linuxtv.org/docs.php for more information
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

//===========================================

#include "bcm_host.h"
#include "ilclient.h"

#define NUMFRAMES 300
#define WIDTH     (320)
#define HEIGHT    (240)
#define PITCH     ((WIDTH+31)&~31)
#define FORMAT V4L2_PIX_FMT_YUV420
#define ENCODER_FORMAT OMX_COLOR_FormatYUV420PackedPlanar

static FILE *outf  = NULL;
static ILCLIENT_T *client = NULL;
static COMPONENT_T *video_encode = NULL;
static COMPONENT_T *list[5];
static int framenumber = 0;

static void print_def(OMX_PARAM_PORTDEFINITIONTYPE def)
{
   printf("Port %lu: %s %lu/%lu %lu %lu %s,%s,%s %lux%lu %lux%lu @%lu %u\n",
	  def.nPortIndex,
	  def.eDir == OMX_DirInput ? "in" : "out",
	  def.nBufferCountActual,
	  def.nBufferCountMin,
	  def.nBufferSize,
	  def.nBufferAlignment,
	  def.bEnabled ? "enabled" : "disabled",
	  def.bPopulated ? "populated" : "not pop.",
	  def.bBuffersContiguous ? "contig." : "not cont.",
	  def.format.video.nFrameWidth,
	  def.format.video.nFrameHeight,
	  def.format.video.nStride,
	  def.format.video.nSliceHeight,
	  def.format.video.xFramerate, def.format.video.eColorFormat);
}

static int video_encode_init()
{
   OMX_VIDEO_PARAM_PORTFORMATTYPE format;
   OMX_PARAM_PORTDEFINITIONTYPE def;
   OMX_ERRORTYPE r;

	   bcm_host_init();
   memset(list, 0, sizeof(list));

	if ((client = ilclient_init()) == NULL) 
	{
		return -3;
	}

	if (OMX_Init() != OMX_ErrorNone) 
	{
	  ilclient_destroy(client);
	  return -4;
	}

   // create video_encode
	r = ilclient_create_component(client, &video_encode, "video_encode", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS | ILCLIENT_ENABLE_OUTPUT_BUFFERS);
	if (r != 0) 
	{
	  printf("ilclient_create_component() for video_encode failed with %x!\n",r);
	  exit(1);
	}
	list[0] = video_encode;

   memset(&def, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
   def.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
   def.nVersion.nVersion = OMX_VERSION;
   def.nPortIndex = 200;

	if (OMX_GetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamPortDefinition,&def) != OMX_ErrorNone) 
	{
		printf("%s:%d: OMX_GetParameter() for video_encode port 200 failed!\n",__FUNCTION__, __LINE__);
		exit(1);
	}

   print_def(def);

   // Port 200: in 1/1 115200 16 enabled,not pop.,not cont. 320x240 320x240 @1966080 20
   def.format.video.nFrameWidth = WIDTH;
   def.format.video.nFrameHeight = HEIGHT;
   def.format.video.xFramerate = 30 << 16;
   def.format.video.nSliceHeight = def.format.video.nFrameHeight;
   def.format.video.nStride = def.format.video.nFrameWidth;
   def.format.video.eColorFormat = ENCODER_FORMAT;

   print_def(def);

   r = OMX_SetParameter(ILC_GET_HANDLE(video_encode),	OMX_IndexParamPortDefinition, &def);
   if (r != OMX_ErrorNone) 
	{
	  printf("%s:%d: OMX_SetParameter() for video_encode port 200 failed with %x!\n", __FUNCTION__, __LINE__, r);
	  exit(1);
   }

   memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
   format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   format.nVersion.nVersion = OMX_VERSION;
   format.nPortIndex = 201;
   format.eCompressionFormat = OMX_VIDEO_CodingAVC;

   printf("OMX_SetParameter for video_encode:201...\n");
   r = OMX_SetParameter(ILC_GET_HANDLE(video_encode), OMX_IndexParamVideoPortFormat, &format);
	if (r != OMX_ErrorNone) 
	{
		printf("%s:%d: OMX_SetParameter() for video_encode port 201 failed with %x!\n", __FUNCTION__, __LINE__, r);
		exit(1);
	}

   printf("encode to idle...\n");
   if (ilclient_change_component_state(video_encode, OMX_StateIdle) == -1) 
	{
	  printf("%s:%d: ilclient_change_component_state(video_encode, OMX_StateIdle) failed", __FUNCTION__, __LINE__);
   }

   printf("enabling port buffers for 200...\n");
   if (ilclient_enable_port_buffers(video_encode, 200, NULL, NULL, NULL) != 0) {
	  printf("enabling port buffers for 200 failed!\n");
	  exit(1);
   }

   printf("enabling port buffers for 201...\n");
   if (ilclient_enable_port_buffers(video_encode, 201, NULL, NULL, NULL) != 0) {
	  printf("enabling port buffers for 201 failed!\n");
	  exit(1);
   }

   printf("encode to executing...\n");
   ilclient_change_component_state(video_encode, OMX_StateExecuting);
}

static int video_encode_test(const void* buffer, size_t size)
{
   OMX_ERRORTYPE r;
   int status = 0;

	OMX_BUFFERHEADERTYPE *buf = ilclient_get_input_buffer(video_encode, 200, 1);
	
	if (buf == NULL) 
	{
		printf("Doh, no buffers for me!\n");
	}
	else 
	{
		/* fill it */
		buf->nFilledLen = size;
		memcpy(buf->pBuffer,buffer,size);
		framenumber++;

		if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_encode), buf) !=	OMX_ErrorNone) 
		{
			printf("Error emptying buffer!\n");
		}

		OMX_BUFFERHEADERTYPE *out = ilclient_get_output_buffer(video_encode, 201, 1);

		r = OMX_FillThisBuffer(ILC_GET_HANDLE(video_encode), out);
		if (r != OMX_ErrorNone) 
		{
			printf("Error filling buffer: %x\n", r);
		}

		if (out != NULL) 
		{
			if (out->nFlags & OMX_BUFFERFLAG_CODECCONFIG) 
			{
				printf("Codec config:\n");
				int i;
				for (i = 0; i < out->nFilledLen; i++)
				{
					printf("%x ", out->pBuffer[i]);
				}
				printf("\n");
			}

			r = fwrite(out->pBuffer, 1, out->nFilledLen, outf);
			if (r != out->nFilledLen) 
			{
				printf("fwrite: Error emptying buffer: %d!\n", r);
			}
			out->nFilledLen = 0;
		}
		else 
		{
			printf("Not getting it :(\n");
		}

	}

   return status;
}

static void video_encode_close()
{
   printf("Teardown.\n");

   printf("disabling port buffers for 200 and 201...\n");
   ilclient_disable_port_buffers(video_encode, 200, NULL, NULL, NULL);
   ilclient_disable_port_buffers(video_encode, 201, NULL, NULL, NULL);

   ilclient_state_transition(list, OMX_StateIdle);
   ilclient_state_transition(list, OMX_StateLoaded);

   ilclient_cleanup_components(list);

   OMX_Deinit();

   ilclient_destroy(client);
}

//===========================================

enum io_method {
		IO_METHOD_MMAP,
		IO_METHOD_USERPTR,
};

struct buffer {
		void   *start;
		size_t  length;
};

static char            *dev_name;
static enum io_method   io = IO_METHOD_MMAP;
static int              fd = -1;
struct buffer          *buffers;
static unsigned int     n_buffers;
static int              out_buf;
static int              force_format;
static int              frame_count = 70;

static void errno_exit(const char *s)
{
		fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
		exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg)
{
		int r;

		do {
				r = ioctl(fh, request, arg);
		} while (-1 == r && EINTR == errno);

		return r;
}

static void process_image(const void *p, int size)
{
		if (out_buf)
		{
				fwrite(p, size, 1, stdout);
		}
		video_encode_test(p,size);

		fflush(stderr);
		fprintf(stderr, ".");
		fflush(stdout);
}

static int read_frame(void)
{
		struct v4l2_buffer buf;
		unsigned int i;

		switch (io) {
		case IO_METHOD_MMAP:
				CLEAR(buf);

				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory = V4L2_MEMORY_MMAP;

				if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
						switch (errno) {
						case EAGAIN:
								return 0;

						case EIO:
								/* Could ignore EIO, see spec. */
								/* fall through */
						default:
								errno_exit("VIDIOC_DQBUF");
						}
				}

				assert(buf.index < n_buffers);

				process_image(buffers[buf.index].start, buf.bytesused);

				if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
						errno_exit("VIDIOC_QBUF");
				break;

		case IO_METHOD_USERPTR:
				CLEAR(buf);

				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory = V4L2_MEMORY_USERPTR;

				if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
						switch (errno) {
						case EAGAIN:
								return 0;

						case EIO:
								/* Could ignore EIO, see spec. */
								/* fall through */
						default:
								errno_exit("VIDIOC_DQBUF");
						}
				}

				for (i = 0; i < n_buffers; ++i)
						if (buf.m.userptr == (unsigned long)buffers[i].start
							&& buf.length == buffers[i].length)
								break;

				assert(i < n_buffers);

				process_image((void *)buf.m.userptr, buf.bytesused);

				if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
						errno_exit("VIDIOC_QBUF");
				break;
		}

		return 1;
}

static void mainloop(void)
{
		unsigned int count;

		count = frame_count;

		while (count-- > 0) 
		{
				for (;;) {
						fd_set fds;
						struct timeval tv;
						int r;

						FD_ZERO(&fds);
						FD_SET(fd, &fds);

						/* Timeout. */
						tv.tv_sec = 2;
						tv.tv_usec = 0;

						r = select(fd + 1, &fds, NULL, NULL, &tv);

						if (-1 == r) {
								if (EINTR == errno)
										continue;
								errno_exit("select");
						}

						if (0 == r) {
								fprintf(stderr, "select timeout\n");
								exit(EXIT_FAILURE);
						}

						if (read_frame())
								break;
						/* EAGAIN - continue select loop. */
				}
		}
}

static void stop_capturing(void)
{
		enum v4l2_buf_type type;

		switch (io) {
		case IO_METHOD_MMAP:
		case IO_METHOD_USERPTR:
				type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
						errno_exit("VIDIOC_STREAMOFF");
				break;
		}
}

static void start_capturing(void)
{
		unsigned int i;
		enum v4l2_buf_type type;

		switch (io) {
		case IO_METHOD_MMAP:
				for (i = 0; i < n_buffers; ++i) {
						struct v4l2_buffer buf;

						CLEAR(buf);
						buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
						buf.memory = V4L2_MEMORY_MMAP;
						buf.index = i;

						if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
								errno_exit("VIDIOC_QBUF");
				}
				type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
						errno_exit("VIDIOC_STREAMON");
				break;

		case IO_METHOD_USERPTR:
				for (i = 0; i < n_buffers; ++i) {
						struct v4l2_buffer buf;

						CLEAR(buf);
						buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
						buf.memory = V4L2_MEMORY_USERPTR;
						buf.index = i;
						buf.m.userptr = (unsigned long)buffers[i].start;
						buf.length = buffers[i].length;

						if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
								errno_exit("VIDIOC_QBUF");
				}
				type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
						errno_exit("VIDIOC_STREAMON");
				break;
		}
}

static void uninit_device(void)
{
		unsigned int i;

		switch (io) {
		case IO_METHOD_MMAP:
				for (i = 0; i < n_buffers; ++i)
						if (-1 == munmap(buffers[i].start, buffers[i].length))
								errno_exit("munmap");
				break;

		case IO_METHOD_USERPTR:
				for (i = 0; i < n_buffers; ++i)
						free(buffers[i].start);
				break;
		}

		free(buffers);
}

static void init_mmap(void)
{
		struct v4l2_requestbuffers req;

		CLEAR(req);

		req.count = 4;
		req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		req.memory = V4L2_MEMORY_MMAP;

		if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
				if (EINVAL == errno) {
						fprintf(stderr, "%s does not support memory mapping\n", dev_name);
						exit(EXIT_FAILURE);
				} else {
						errno_exit("VIDIOC_REQBUFS");
				}
		}

		if (req.count < 2) {
				fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
				exit(EXIT_FAILURE);
		}

		buffers = calloc(req.count, sizeof(*buffers));

		if (!buffers) {
				fprintf(stderr, "Out of memory\n");
				exit(EXIT_FAILURE);
		}

		for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
				struct v4l2_buffer buf;

				CLEAR(buf);

				buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory      = V4L2_MEMORY_MMAP;
				buf.index       = n_buffers;

				if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
						errno_exit("VIDIOC_QUERYBUF");

				buffers[n_buffers].length = buf.length;
				buffers[n_buffers].start =	mmap(NULL /* start anywhere */,
							  buf.length,
							  PROT_READ | PROT_WRITE /* required */,
							  MAP_SHARED /* recommended */,
							  fd, buf.m.offset);

				if (MAP_FAILED == buffers[n_buffers].start)
						errno_exit("mmap");
		}		
}

static void init_userp(unsigned int buffer_size)
{
		struct v4l2_requestbuffers req;

		CLEAR(req);

		req.count  = 4;
		req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		req.memory = V4L2_MEMORY_USERPTR;

		if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
				if (EINVAL == errno) {
						fprintf(stderr, "%s does not support ""user pointer i/o\n", dev_name);
						exit(EXIT_FAILURE);
				} else {
						errno_exit("VIDIOC_REQBUFS");
				}
		}

		buffers = calloc(4, sizeof(*buffers));

		if (!buffers) {
				fprintf(stderr, "Out of memory\n");
				exit(EXIT_FAILURE);
		}

		for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
				buffers[n_buffers].length = buffer_size;
				buffers[n_buffers].start = malloc(buffer_size);

				if (!buffers[n_buffers].start) {
						fprintf(stderr, "Out of memory\n");
						exit(EXIT_FAILURE);
				}
		}

}

static void init_read(__u32 sizeImage)
{
	switch (io) {
		case IO_METHOD_MMAP:
				init_mmap();
				break;

		case IO_METHOD_USERPTR:
				init_userp(sizeImage);
				break; 
	}
		
	printf("n_buffer:%d\n", n_buffers);	
}
	
static void init_device(void)
{
		struct v4l2_capability cap;
		struct v4l2_cropcap cropcap;
		struct v4l2_crop crop;
		struct v4l2_format fmt;
		unsigned int min;

		if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
				if (EINVAL == errno) {
						fprintf(stderr, "%s is no V4L2 device\n",dev_name);
						exit(EXIT_FAILURE);
				} else {
						errno_exit("VIDIOC_QUERYCAP");
				}
		}

		if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
				fprintf(stderr, "%s is no video capture device\n", dev_name);
				exit(EXIT_FAILURE);
		}

		switch (io) {
			case IO_METHOD_MMAP:
			case IO_METHOD_USERPTR:
				if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
						fprintf(stderr, "%s does not support streaming i/o\n",
								 dev_name);
						exit(EXIT_FAILURE);
				}
				break;
		}


		/* Select video input, video standard and tune here. */
		CLEAR(cropcap);

		cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
				crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				crop.c = cropcap.defrect; /* reset to default */

				if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
						switch (errno) {
						case EINVAL:
								/* Cropping not supported. */
								break;
						default:
								/* Errors ignored. */
								break;
						}
				}
		} else {
				/* Errors ignored. */
		}


		CLEAR(fmt);

		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (force_format) 
		{
				fmt.fmt.pix.width       = WIDTH;
				fmt.fmt.pix.height      = HEIGHT;
				fmt.fmt.pix.pixelformat = FORMAT;
				fmt.fmt.pix.field       = V4L2_FIELD_NONE;

				if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
						errno_exit("VIDIOC_S_FMT");

				/* Note VIDIOC_S_FMT may change width and height. */
		} else {
				/* Preserve original settings as set by v4l2-ctl for example */
				if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
						errno_exit("VIDIOC_G_FMT");
		}

		/* Buggy driver paranoia. */
		min = fmt.fmt.pix.width * 2;
		if (fmt.fmt.pix.bytesperline < min)
		{
				fmt.fmt.pix.bytesperline = min;
		}
		min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
		if (fmt.fmt.pix.sizeimage < min)
		{
				fmt.fmt.pix.sizeimage = min;
		}

		init_read(fmt.fmt.pix.sizeimage);
}

static void open_device(void)
{
		struct stat st;

		if (-1 == stat(dev_name, &st)) {
				fprintf(stderr, "Cannot identify '%s': %d, %s\n",
						 dev_name, errno, strerror(errno));
				exit(EXIT_FAILURE);
		}

		if (!S_ISCHR(st.st_mode)) {
				fprintf(stderr, "%s is no device\n", dev_name);
				exit(EXIT_FAILURE);
		}

		fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

		if (-1 == fd) {
				fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno, strerror(errno));
				exit(EXIT_FAILURE);
		}
}

static void close_device(void)
{
		if (-1 == close(fd))
				errno_exit("close");

		fd = -1;
}


static void usage(FILE *fp, int argc, char **argv)
{
		fprintf(fp,
				 "Usage: %s [options]\n\n"
				 "Version 1.3\n"
				 "Options:\n"
				 "-d | --device name   Video device name [%s]\n"
				 "-h | --help          Print this message\n"
				 "-m | --mmap          Use memory mapped buffers [default]\n"
				 "-r | --read          Use read() calls\n"
				 "-u | --userp         Use application allocated buffers\n"
				 "-o | --output        Outputs stream to stdout\n"
				 "-f | --format        Force format to 640x480 YUYV\n"
				 "-c | --count         Number of frames to grab [%i]\n"
				 "",
				 argv[0], dev_name, frame_count);
}

static const char short_options[] = "d:hmruofc:";

static const struct option
long_options[] = {
		{ "device", required_argument, NULL, 'd' },
		{ "help",   no_argument,       NULL, 'h' },
		{ "mmap",   no_argument,       NULL, 'm' },
		{ "userp",  no_argument,       NULL, 'u' },
		{ "output", no_argument,       NULL, 'o' },
		{ "format", no_argument,       NULL, 'f' },
		{ "count",  required_argument, NULL, 'c' },
		{ 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
		dev_name = "/dev/video0";

		for (;;) {
				int idx;
				int c;

				c = getopt_long(argc, argv,
								short_options, long_options, &idx);

				if (-1 == c)
						break;

				switch (c) {
				case 0: /* getopt_long() flag */
						break;

				case 'd':
						dev_name = optarg;
						break;

				case 'h':
						usage(stdout, argc, argv);
						exit(EXIT_SUCCESS);

				case 'm':
						io = IO_METHOD_MMAP;
						break;

				case 'u':
						io = IO_METHOD_USERPTR;
						break;

				case 'o':
						out_buf++;
						break;

				case 'f':
						force_format++;
						break;

				case 'c':
						errno = 0;
						frame_count = strtol(optarg, NULL, 0);
						if (errno)
								errno_exit(optarg);
						break;

				default:
						usage(stderr, argc, argv);
						exit(EXIT_FAILURE);
				}
		}
		const char* outputfilename="out.h264";
		outf = fopen(outputfilename, "w");
		if (outf == NULL) {
		  printf("Failed to open '%s' for writing video\n", outputfilename);
		  exit(1);
		}
		int ret = video_encode_init();
		if (ret != 0)
		{
		  printf("Failed to init GPU :%d\n", ret);
		  exit(1);
		}	
		

		open_device();
		init_device();
		start_capturing();
		mainloop();
		stop_capturing();
		uninit_device();
		close_device();
	
		video_encode_close();
		fclose(outf);
		
		fprintf(stderr, "\n");
		return 0;
}
