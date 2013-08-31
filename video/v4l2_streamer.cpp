/* V4L2 RTSP streamer */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <sys/timeb.h>
#include <signal.h>
#include <string>
#include <iostream>
#include <list>

// libv4l2
#include <libv4l2.h>

// live555
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

#define DEBUG 0

class V4L2DeviceSource: public FramedSource 
{
	public:
		// V4L2 Capture parameters
		class V4L2DeviceParameters 
		{
			public:
				V4L2DeviceParameters(const char* devname, int format, int queueSize, int width, int height) : 
					m_devName(devname), m_format(format), m_queueSize(queueSize), m_width(width), m_height(height) {};
					
				std::string m_devName;
				int m_width;
				int m_height;
				int m_format;
				int m_queueSize;
				
		};

		// Captured frame
		struct Frame
		{
			Frame(char* buffer, int size) : m_buffer(buffer), m_size(size) {};
			~Frame()  { delete m_buffer; };
			char* m_buffer;
			int m_size;
		};
		
	public:
		static V4L2DeviceSource* createNew(UsageEnvironment& env, V4L2DeviceParameters params) 
		{ 
			V4L2DeviceSource* device = new V4L2DeviceSource(env, params); 
			if (device && !device->init())
			{
				delete device;
				device=NULL;
			}
			return device;
		}
		int getBufferSize() { return 2*m_bufferSize; };

	protected:
		V4L2DeviceSource(UsageEnvironment& env, V4L2DeviceParameters params) : FramedSource(env), m_params(params), m_fd(-1), m_bufferSize(0)
		{
		}
		
		virtual ~V4L2DeviceSource()
		{
			v4l2_close(m_fd);
		}

		bool init()
		{
			m_fd = initdevice(m_params.m_devName.c_str());
			if (m_fd == -1)
			{
				fprintf(stderr, "Init device:%s failure\n", m_params.m_devName.c_str());
			}
			else
			{
				envir().taskScheduler().turnOnBackgroundReadHandling( m_fd, V4L2DeviceSource::incomingPacketHandlerStub,this);		
			}
			return (m_fd!=-1);
		}
		

		static int xioctl(int fd, int request, void *arg)
		{
			int r = -1;

			do 
			{
				r = v4l2_ioctl(fd, request, arg);
			} while (r == -1 && ((errno == EINTR) || (errno == EAGAIN)));

			return r;
		}
		
		int initdevice(const char *dev_name)
		{
			struct v4l2_format              fmt;

			int fd = v4l2_open(dev_name, O_RDWR | O_NONBLOCK, 0);
			if (fd < 0) 
			{
				perror("Cannot open device");
				return -1;
			}
			
			// ===== CAPAPILITIES
			struct v4l2_capability cap;
			memset(&(cap), 0, sizeof(cap));
			if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) 
			{
				fprintf(stderr, "xioctl cannot get capabilities error %d, %s\n", errno, strerror(errno));
				return -1;
			}
			fprintf(stderr, "driver:%s capabilities;%X\n", cap.driver, cap.capabilities);

			if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) 
			{
				fprintf(stderr, "%s is no video capture device\n", dev_name);
				return -1;
			}
			if ((cap.capabilities & V4L2_CAP_READWRITE))
			{
				fprintf(stderr, "%s support read i/o\n", dev_name);
			}
			if ((cap.capabilities & V4L2_CAP_STREAMING)) 
			{
				fprintf(stderr, "%s support streaming i/o\n", dev_name);
			}	
			
			// ===== FORMAT
			memset(&(fmt), 0, sizeof(fmt));
			fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			fmt.fmt.pix.width       = m_params.m_width;
			fmt.fmt.pix.height      = m_params.m_height;
			fmt.fmt.pix.pixelformat = m_params.m_format;
			fmt.fmt.pix.field       = V4L2_FIELD_ANY;
			
			if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1)
			{
				fprintf(stderr, "xioctl cannot set format error %d, %s\n", errno, strerror(errno));
				return -1;
			}
			if (fmt.fmt.pix.pixelformat != m_params.m_format) 
			{
				printf("Libv4l didn't accept format (%d). Can't proceed.\n", m_params.m_format);
				return -1;
			}
			if ((fmt.fmt.pix.width != m_params.m_width) || (fmt.fmt.pix.height != m_params.m_height))
			{
				printf("Warning: driver is sending image at %dx%d\n", fmt.fmt.pix.width, fmt.fmt.pix.width);
			}

			m_bufferSize =  fmt.fmt.pix.sizeimage;
			
			return fd;
		}
		
		virtual void doGetNextFrame()
		{
			static int send_fps = 0;
			static int send_fps_sec = 0;
			gettimeofday(&fPresentationTime, NULL);
			
			fDurationInMicroseconds = 0;
			fFrameSize = 0;
			
			if (m_captureQueue.empty())
			{
#if DEBUG				
				envir() << "Queue is empty \n";		
#endif				
			}
			else
			{
				send_fps++;
				if (fPresentationTime.tv_sec != send_fps_sec)
				{
					envir() << "V4L2DeviceSource::doGetNextFrame queuesize:"  << m_captureQueue.size() << "fps:" << send_fps <<"\n";		
					send_fps_sec = fPresentationTime.tv_sec;
					send_fps = 0;
				}
				Frame * frame = m_captureQueue.back();
				m_captureQueue.pop_back();
												
				if (frame->m_size > fMaxSize) 
				{
					fFrameSize = fMaxSize;
					fNumTruncatedBytes = frame->m_size - fMaxSize;
				} 
				else 
				{
					fFrameSize = frame->m_size;
				}
				memcpy(fTo, frame->m_buffer, fFrameSize);
				delete frame;
			}
			nextTask() = envir().taskScheduler().scheduleDelayedTask(0,(TaskFunc*)FramedSource::afterGetting, this);
		}
		
		static void incomingPacketHandlerStub(void* clientData, int mask)
		{
			V4L2DeviceSource* source = (V4L2DeviceSource*) clientData;
			source->incomingPacketHandler(mask);
		}	
		
		void incomingPacketHandler(int mask) 
		{
			char* buffer = new char[m_bufferSize];
			
			struct timeb ref;
			ftime(&ref); 
			int frameSize = v4l2_read(m_fd, buffer,  m_bufferSize);
			
			if (frameSize < 0)
			{
				envir() << "V4L2DeviceSource::incomingPacketHandler fd:"  << m_fd << " mask:" << mask << " errno:" << errno << " "  << strerror(errno) << "\n";		
				delete buffer;
				handleClosure(this);
			}
			else
			{
				struct timeb current;
				ftime(&current); 
#if DEBUG
				envir() << "V4L2DeviceSource::incomingPacketHandler read time:"  << int((current.time-ref.time)*1000 + current.millitm-ref.millitm) << " ms size:" << frameSize << "\n";		
				printf ("%02X%02X%02X%02X%02X%02X\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
#endif
				if (m_captureQueue.size() >= m_params.m_queueSize)
				{
					while (m_captureQueue.size() >= m_params.m_queueSize)
					{
#if DEBUG						
						envir() << "Queue full size drop frame size:"  << m_captureQueue.size() << " \n";		
#endif						
						delete m_captureQueue.back();
						m_captureQueue.pop_back();
					}
				}
				m_captureQueue.push_back(new Frame(buffer, frameSize));	  				
			}
		}	

	private:
		V4L2DeviceParameters m_params;
		int m_fd;
		int m_bufferSize;
		std::list<Frame*> m_captureQueue;
};

char quit = 0;
void sighandler(int n)
{ 
	printf("SIGINT\n");
	quit =1;
}

RTPSink* createSink(UsageEnvironment* env, Groupsock * gs, int format)
{
	RTPSink* videoSink = NULL;
	switch (format)
	{
		case V4L2_PIX_FMT_H264 : videoSink = H264VideoRTPSink::createNew(*env, gs,96); break;
	}
	return videoSink;
}

FramedSource* createSource(UsageEnvironment* env, FramedSource * videoES, int format)
{
	FramedSource* source = NULL;
	switch (format)
	{
		case V4L2_PIX_FMT_H264 : source = H264VideoStreamFramer::createNew(*env, videoES); break;
	}
	return source;
}

int main(int argc, char** argv) 
{
	char *dev_name = "/dev/video0";	
	int format = V4L2_PIX_FMT_H264;
	int width = 640;
	int height = 480;
	int queueSize = 100;

	int c = 0;     
	while ((c = getopt (argc, argv, "hW:H:Q:")) != -1)
	{
		switch (c)
		{
			case 'W':	width = atoi(optarg); break;
			case 'H':	height = atoi(optarg); break;
			case 'Q':	queueSize = atoi(optarg); break;
			case 'h':
			{
				std::cout << argv[0] << " [-Q queueSize] [-W width] [-H height] [device]" << std::endl;
				exit(0);
			}
		}
	}
	if (optind<argc)
	{
		dev_name = argv[optind];
	}
     
	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);	
	
	RTSPServer* rtspServer = RTSPServer::createNew(*env, 8554);
	if (rtspServer == NULL) 
	{
		*env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
	}
	else
	{		
		// Init capture
		*env << "Create V4L2 Source..." << dev_name << "\n";
		V4L2DeviceSource::V4L2DeviceParameters param(dev_name,format,queueSize,width,height);
		V4L2DeviceSource* videoES = V4L2DeviceSource::createNew(*env, param);
		if (videoES == NULL) 
		{
			*env << "Unable to create source \n";
		}
		else
		{
			*env << "Create Framed Source...\n";
			FramedSource* videoSource = createSource(env, videoES, format);

			// Create Sink
			struct in_addr destinationAddress;
			destinationAddress.s_addr = chooseRandomIPv4SSMAddress(*env);

			const unsigned short rtpPortNum = 18888;
			const unsigned short rtcpPortNum = rtpPortNum+1;
			const unsigned char ttl = 255;

			const Port rtpPort(rtpPortNum);
			const Port rtcpPort(rtcpPortNum);

			Groupsock rtpGroupsock(*env, destinationAddress, rtpPort, ttl);
			Groupsock rtcpGroupsock(*env, destinationAddress, rtcpPort, ttl);

			// Create a 'Video RTP' sink from the RTP 'groupsock':
			OutPacketBuffer::maxSize = videoES->getBufferSize();
			RTPSink* videoSink = createSink(env,&rtpGroupsock, format);

			// Create 'RTCP instance' for this RTP sink:
			const unsigned maxCNAMElen = 100;
			unsigned char CNAME[maxCNAMElen+1];
			gethostname((char*)CNAME, maxCNAMElen);
			CNAME[maxCNAMElen] = '\0'; 
			RTCPInstance* rtcp = RTCPInstance::createNew(*env, &rtcpGroupsock,  500, CNAME, videoSink, NULL);
			
			// Create Server Session
			ServerMediaSession* sms = ServerMediaSession::createNew(*env);
			sms->addSubsession(PassiveServerMediaSubsession::createNew(*videoSink, rtcp));
			rtspServer->addServerMediaSession(sms);

			char* url = rtspServer->rtspURL(sms);
			*env << "Play this stream using the URL \"" << url << "\"\n";
			delete[] url;

			
			// Finally, start playing:
			*env << "Starting...\n";
			videoSink->startPlaying(*videoSource, NULL, videoSink);

			signal(SIGINT,sighandler);
			env->taskScheduler().doEventLoop(&quit); 
			*env << "Exiting..\n";
			
			Medium::close(videoSink);
		}

		Medium::close(rtspServer);
	}
	
	env->reclaim();
	delete scheduler;
	
	
	return 0;
}

