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

// V4L2 FramedSource
// ---------------------------------
class V4L2DeviceSource: public FramedSource 
{
	public:
		// V4L2 Capture parameters
		// ---------------------------------
		struct V4L2DeviceParameters 
		{
			V4L2DeviceParameters(const char* devname, int format, int queueSize, int width, int height, int fps) : 
				m_devName(devname), m_format(format), m_queueSize(queueSize), m_width(width), m_height(height), m_fps(fps) {};
				
			std::string m_devName;
			int m_width;
			int m_height;
			int m_format;
			int m_queueSize;
			int m_fps;			
		};

		// Captured frame
		// ---------------------------------
		struct Frame
		{
			Frame(char* buffer, int size, timeval timestamp) : m_buffer(buffer), m_size(size), m_timestamp(timestamp) { };
			~Frame()  { delete m_buffer; };
			
			char* m_buffer;
			int m_size;
			timeval m_timestamp;
		};
		
		// compute FPS
		// ---------------------------------
		class Fps
		{
			public:
				Fps(const std::string & msg) : m_fps(0), m_fps_sec(0), m_msg(msg) {};
				
			public:
				int notify(int tv_sec)
				{
					m_fps++;
					if (tv_sec != m_fps_sec)
					{
						std::cout << m_msg  << "tv_sec:" <<   tv_sec << " fps:" << m_fps <<"\n";		
						m_fps_sec = tv_sec;
						m_fps = 0;
					}
					return m_fps;
				}
			
			
			protected:
				int m_fps;
				int m_fps_sec;
				const std::string m_msg;
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
		int getBufferSize() { return m_bufferSize; };

	protected:
		V4L2DeviceSource(UsageEnvironment& env, V4L2DeviceParameters params) : FramedSource(env), m_params(params), m_fd(-1), m_bufferSize(0), m_in("in"), m_out("out") 
		{
			m_eventTriggerId = envir().taskScheduler().createEventTrigger(V4L2DeviceSource::deliverFrameStub);
		}
		
		virtual ~V4L2DeviceSource()
		{
			envir().taskScheduler().deleteEventTrigger(m_eventTriggerId);
			v4l2_close(m_fd);
		}

	protected:
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
		
		int xioctl(int fd, int request, void *arg)
		{
			int r = -1;

			do 
			{
				r = v4l2_ioctl(fd, request, arg);
			} while (r == -1 && ((errno == EINTR) || (errno == EAGAIN)));

			return r;
		}
		
		int checkCapabilities(int fd)
		{
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
				fprintf(stderr, "%s is no video capture device\n", m_params.m_devName.c_str());
				return -1;
			}
			
			if ((cap.capabilities & V4L2_CAP_READWRITE)) fprintf(stderr, "%s support read i/o\n", m_params.m_devName.c_str());
			if ((cap.capabilities & V4L2_CAP_STREAMING))  fprintf(stderr, "%s support streaming i/o\n", m_params.m_devName.c_str());
			if ((cap.capabilities & V4L2_CAP_TIMEPERFRAME)) fprintf(stderr, "%s support timeperframe\n", m_params.m_devName.c_str());
			
			return 0;
		}

		int configureFormat(int fd)
		{
			struct v4l2_format   fmt;			
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
			
			fprintf(stderr, "fmt.fmt.pix.field:%X\n", fmt.fmt.pix.field);
			
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
			return 0;
		}

		int configureParam(int fd)
		{
			struct v4l2_streamparm   param;			
			memset(&(param), 0, sizeof(param));
			param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			param.parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
			param.parm.capture.timeperframe.numerator = 1;
			param.parm.capture.timeperframe.denominator = m_params.m_fps;

			if (xioctl(fd, VIDIOC_S_PARM, &param) == -1)
			{
				fprintf(stderr, "xioctl cannot set param error %d, %s\n", errno, strerror(errno));
				return -1;
			}
			
			fprintf(stderr, "fps :%d/%d nbBuffer:%d\n", param.parm.capture.timeperframe.numerator, param.parm.capture.timeperframe.denominator, param.parm.capture.readbuffers);
			
			return 0;
		}
		
		int initdevice(const char *dev_name)
		{
			int fd = v4l2_open(dev_name, O_RDWR | O_NONBLOCK, 0);
			if (fd < 0) 
			{
				perror("Cannot open device");
				return -1;
			}
			
			if (checkCapabilities(fd) !=0)
			{
				return -1;
			}
			
			if (configureFormat(fd) !=0)
			{
				return -1;
			}

			if (configureParam(fd) !=0)
			{
				return -1;
			}
			
			return fd;
		}
		
		virtual void doGetNextFrame()
		{
		}
		
		static void deliverFrameStub(void* clientData)
		{
			V4L2DeviceSource* source = (V4L2DeviceSource*) clientData;
			source->deliverFrame();
		}	
		
		virtual void deliverFrame()
		{			
			if (!isCurrentlyAwaitingData()) return;
			
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
				gettimeofday(&fPresentationTime, NULL);			
				m_out.notify(fPresentationTime.tv_sec);
				
				Frame * frame = m_captureQueue.front();
				m_captureQueue.pop_front();
												
				if (frame->m_size > fMaxSize) 
				{
					fFrameSize = fMaxSize;
					fNumTruncatedBytes = frame->m_size - fMaxSize;
				} 
				else 
				{
					fFrameSize = frame->m_size;
				}
				timeval diff;
				timersub(&fPresentationTime,&(frame->m_timestamp),&diff);
#if DEBUG				
				envir() << "Send frame:" << (int)frame->m_timestamp.tv_sec  <<  " " << (int)frame->m_timestamp.tv_usec << " " << (int)(diff.tv_sec*1000+diff.tv_usec/1000) << "ms\n";						
#endif				
				memcpy(fTo, frame->m_buffer, fFrameSize);
				delete frame;
			}
			FramedSource::afterGetting(this);
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
				
				int fps = m_in.notify(current.time);
#if DEBUG
				envir() << "V4L2DeviceSource::incomingPacketHandler read time:"  << int((current.time-ref.time)*1000 + current.millitm-ref.millitm) << " ms size:" << frameSize << "\n";		
				printf ("%02X%02X%02X%02X%02X%02X%02X%02X\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
				char marker[] = {0,0,0,1};
				for (int i=1; i<frameSize-4; ++i)
				{
					if (memcmp(&buffer[i],&marker,sizeof(marker))==0) printf ("match %d\n",i);
				}
#endif
				while (m_captureQueue.size() >= m_params.m_queueSize)
				{
#if DEBUG						
					envir() << "Queue full size drop frame size:"  << m_captureQueue.size() << " \n";		
#endif						
					delete m_captureQueue.front();
					m_captureQueue.pop_front();
				}
				timeval tv;
				tv.tv_sec = current.time;
				tv.tv_usec = current.millitm * 1000;
				m_captureQueue.push_back(new Frame(buffer, frameSize,tv));	  				
				envir().taskScheduler().triggerEvent(m_eventTriggerId, this);
			}
		}	

	private:
		V4L2DeviceParameters m_params;
		int m_fd;
		int m_bufferSize;
		std::list<Frame*> m_captureQueue;
		Fps m_in;
		Fps m_out;
		EventTriggerId m_eventTriggerId;
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
	// default parameters
	char *dev_name = "/dev/video0";	
	int format = V4L2_PIX_FMT_H264;
	int width = 640;
	int height = 480;
	int queueSize = 100;
	int fps = 25;
	unsigned short rtpPortNum = 20000;
	unsigned short rtcpPortNum = rtpPortNum+1;
	unsigned char ttl = 255;
	struct in_addr destinationAddress;
	unsigned short rtspPort = 8554;

	// decode parameters
	int c = 0;     
	while ((c = getopt (argc, argv, "hW:H:Q:P:F:")) != -1)
	{
		switch (c)
		{
			case 'W':	width = atoi(optarg); break;
			case 'H':	height = atoi(optarg); break;
			case 'Q':	queueSize = atoi(optarg); break;
			case 'P':	rtspPort = atoi(optarg); break;
			case 'F':	fps = atoi(optarg); break;
			case 'h':
			{
				std::cout << argv[0] << " [-P RTSP_port] [-Q queueSize] [-W width] [-H height] [-F fps] [device]" << std::endl;
				exit(0);
			}
		}
	}
	if (optind<argc)
	{
		dev_name = argv[optind];
	}
     
	// start th job
	signal(SIGINT,sighandler);
	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);	
	
	RTSPServer* rtspServer = RTSPServer::createNew(*env, rtspPort);
	if (rtspServer == NULL) 
	{
		*env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
	}
	else
	{		
		// Init capture
		*env << "Create V4L2 Source..." << dev_name << "\n";
		V4L2DeviceSource::V4L2DeviceParameters param(dev_name,format,queueSize,width,height,fps);
		V4L2DeviceSource* videoES = V4L2DeviceSource::createNew(*env, param);
		if (videoES == NULL) 
		{
			*env << "Unable to create source \n";
		}
		else
		{
			*env << "Create Framed Source...\n";
			FramedSource* videoSource = createSource(env, videoES, format);

			// Create RTP/RTCP groupsock
			destinationAddress.s_addr = chooseRandomIPv4SSMAddress(*env);	
			Groupsock rtpGroupsock(*env, destinationAddress, Port(rtpPortNum), ttl);
			Groupsock rtcpGroupsock(*env, destinationAddress, Port(rtcpPortNum), ttl);

			// Create a RTP sink from the RTP 'groupsock':
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

			// print the session URL
			char* url = rtspServer->rtspURL(sms);
			*env << "Play this stream using the URL \"" << url << "\"\n";
			delete[] url;
			
			// Start playing:
			*env << "Starting...\n";
			videoSink->startPlaying(*videoSource, NULL, videoSink);
			
			// main loop
			env->taskScheduler().doEventLoop(&quit); 
			*env << "Exiting..\n";			
		}

		Medium::close(rtspServer);
	}
	
	env->reclaim();
	delete scheduler;
	
	
	return 0;
}

