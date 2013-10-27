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

// V4L2 FramedSource
// ---------------------------------
class V4L2DeviceSource: public FramedSource 
{
	public:
		// V4L2 Capture parameters
		// ---------------------------------
		struct V4L2DeviceParameters 
		{
			V4L2DeviceParameters(const char* devname, int format, int queueSize, int width, int height, int fps, bool verbose) : 
				m_devName(devname), m_format(format), m_queueSize(queueSize), m_width(width), m_height(height), m_fps(fps), m_verbose(verbose) {};
				
			std::string m_devName;
			int m_width;
			int m_height;
			int m_format;
			int m_queueSize;
			int m_fps;			
			bool m_verbose;
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
				if (m_params.m_verbose) 
				{
					envir() << "Queue is empty \n";		
				}
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
				
				if (m_params.m_verbose) 
				{
					envir() << "deliverFrame timestamp:" << (int)frame->m_timestamp.tv_sec  <<  " " << (int)frame->m_timestamp.tv_usec << " " << (int)(diff.tv_sec*1000+diff.tv_usec/1000) << "ms size:" << fFrameSize << "\n";						
				}

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
				if (m_params.m_verbose) 
				{
					envir() << "readFrame time:"  << int((current.time-ref.time)*1000 + current.millitm-ref.millitm) << " ms size:" << frameSize << "\n";		
				}
				queueFrame(buffer,frameSize);
			}			
		}	
		
		void queueFrame(char * frame, int frameSize) 
		{
			while (m_captureQueue.size() >= m_params.m_queueSize)
			{
				if (m_params.m_verbose) 
				{
					envir() << "Queue full size drop frame size:"  << m_captureQueue.size() << " \n";		
				}
				delete m_captureQueue.front();
				m_captureQueue.pop_front();
			}
			timeval tv;
			gettimeofday(&tv, NULL);								
			if (m_params.m_verbose) 
			{
				printf ("queue frame size:%d data:%02X%02X%02X%02X%02X...\n", frameSize, frame[0], frame[1], frame[2], frame[3], frame[4]);
			}
			m_captureQueue.push_back(new Frame(frame, frameSize,tv));	  				
			envir().taskScheduler().triggerEvent(m_eventTriggerId, this);
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

FramedSource* createSource(UsageEnvironment& env, FramedSource * videoES, int format)
{
	FramedSource* source = NULL;
	switch (format)
	{
		case V4L2_PIX_FMT_H264 : source = H264VideoStreamFramer::createNew(env, videoES); break;
	}
	return source;
}

RTPSink* createSink(UsageEnvironment& env, Groupsock * rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, int format)
{
	RTPSink* videoSink = NULL;
	switch (format)
	{
		case V4L2_PIX_FMT_H264 : videoSink = H264VideoRTPSink::createNew(env, rtpGroupsock,rtpPayloadTypeIfDynamic); break;
	}
	return videoSink;
}

class MulticastServerMediaSubsession : public PassiveServerMediaSubsession 
{
	public:
		static MulticastServerMediaSubsession* createNew(UsageEnvironment& env, struct in_addr destinationAddress, Port rtpPortNum, Port rtcpPortNum, int ttl, StreamReplicator* replicator, int format) 
		{ 
			FramedSource* source = replicator->createStreamReplica();
			
			FramedSource* videoSource = createSource(env, source, format);

			// Create RTP/RTCP groupsock
			Groupsock* rtpGroupsock = new Groupsock(env, destinationAddress, rtpPortNum, ttl);
			Groupsock* rtcpGroupsock = new Groupsock(env, destinationAddress, rtcpPortNum, ttl);

			// Create a RTP sink from the RTP 'groupsock':
			RTPSink* videoSink = createSink(env,rtpGroupsock, 96, format);

			// Create 'RTCP instance' for this RTP sink:
			const unsigned maxCNAMElen = 100;
			unsigned char CNAME[maxCNAMElen+1];
			gethostname((char*)CNAME, maxCNAMElen);
			CNAME[maxCNAMElen] = '\0'; 
			RTCPInstance* rtcpInstance = RTCPInstance::createNew(env, rtcpGroupsock,  500, CNAME, videoSink, NULL);

			// start 
			videoSink->startPlaying(*videoSource, NULL, NULL);
			
			return new MulticastServerMediaSubsession(*videoSink,rtcpInstance);
		}
		
	protected:
		MulticastServerMediaSubsession(RTPSink& rtpSink, RTCPInstance* rtcpInstance) : PassiveServerMediaSubsession(rtpSink, rtcpInstance) {};			
};

class UnicastServerMediaSubsession : public OnDemandServerMediaSubsession 
{
	public:
		static UnicastServerMediaSubsession* createNew(UsageEnvironment& env, StreamReplicator* replicator, int format) 
		{ 
			return new UnicastServerMediaSubsession(env,replicator,format);
		}
		
	protected:
		UnicastServerMediaSubsession(UsageEnvironment& env, StreamReplicator* replicator, int format) : OnDemandServerMediaSubsession(env, False), m_replicator(replicator), m_format(format) {};
			
		virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate)
		{
			FramedSource* source = m_replicator->createStreamReplica();
			return createSource(envir(), source, m_format);
		}
		virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,  unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource)
		{
			return createSink(envir(), rtpGroupsock,rtpPayloadTypeIfDynamic, m_format);
		}
		virtual char const* getAuxSDPLine(RTPSink* rtpSink,FramedSource* inputSource)
		{
			std::cout << "hardcoded SPS/PPS !!!!!!!!!" << std::endl;
			return strdup("a=fmtp:96 packetization-mode=1;profile-level-id=64001F;sprop-parameter-sets=J2QAH6wrQFAe0A8SJqA=,KO4G8sA=\n");
		}
	protected:
		StreamReplicator* m_replicator;
		int m_format;
};

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
	bool verbose = false;

	// decode parameters
	int c = 0;     
	while ((c = getopt (argc, argv, "hW:H:Q:P:F:v")) != -1)
	{
		switch (c)
		{
			case 'v':	verbose = true; break;
			case 'W':	width = atoi(optarg); break;
			case 'H':	height = atoi(optarg); break;
			case 'Q':	queueSize = atoi(optarg); break;
			case 'P':	rtspPort = atoi(optarg); break;
			case 'F':	fps = atoi(optarg); break;
			case 'h':
			{
				std::cout << argv[0] << " [-v] [-m] [-P RTSP_port] [-Q queueSize] [-W width] [-H height] [-F fps] [device]" << std::endl;
				exit(0);
			}
		}
	}
	if (optind<argc)
	{
		dev_name = argv[optind];
	}
     
	// 
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
		V4L2DeviceSource::V4L2DeviceParameters param(dev_name,format,queueSize,width,height,fps,verbose);
		V4L2DeviceSource* videoES = V4L2DeviceSource::createNew(*env, param);
		if (videoES == NULL) 
		{
			*env << "Unable to create source \n";
		}
		else
		{
			destinationAddress.s_addr = chooseRandomIPv4SSMAddress(*env);	
			OutPacketBuffer::maxSize = videoES->getBufferSize();
			StreamReplicator* replicator = StreamReplicator::createNew(*env, videoES);

			// Create Server Unicast Session
			{
				FramedSource* source = replicator->createStreamReplica();
				ServerMediaSession* sms = ServerMediaSession::createNew(*env, "unicast");
				sms->addSubsession(UnicastServerMediaSubsession::createNew(*env,replicator,format));
				rtspServer->addServerMediaSession(sms);

				char* url = rtspServer->rtspURL(sms);
				*env << "Play this stream using the URL \"" << url << "\"\n";
				delete[] url;			
			}

			// Create Server Multicast Session
			{
				ServerMediaSession* sms = ServerMediaSession::createNew(*env, "multicast");
				sms->addSubsession(MulticastServerMediaSubsession::createNew(*env,destinationAddress, Port(rtpPortNum), Port(rtcpPortNum), ttl, replicator,format));
				rtspServer->addServerMediaSession(sms);

				char* url = rtspServer->rtspURL(sms);
				*env << "Play this stream using the URL \"" << url << "\"\n";
				delete[] url;			
			}

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

