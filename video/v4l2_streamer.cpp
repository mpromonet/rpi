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

#include <libv4l2.h>

#include <string>
#include <signal.h>

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

class V4L2DeviceSource: public FramedSource 
{
	public:
		class V4L2DeviceParameters 
		{
			public:
				V4L2DeviceParameters(const char* devname) : m_devName(devname) {};
					
				std::string m_devName;
				static const int m_width = 320;
				static const int m_height = 240;
				static const int m_format = V4L2_PIX_FMT_MJPEG;
				
		};

	public:
		static V4L2DeviceSource* createNew(UsageEnvironment& env, V4L2DeviceParameters params) { return new V4L2DeviceSource(env, params); }

	protected:
		V4L2DeviceSource(UsageEnvironment& env, V4L2DeviceParameters params) : FramedSource(env), m_params(params), m_fd(-1), m_bufferSize(0)
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
		}
		
		virtual ~V4L2DeviceSource()
		{
			v4l2_close(m_fd);
		}

	private:
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

			m_bufferSize =  (fmt.fmt.pix.width * fmt.fmt.pix.width);
			
			return fd;
		}
		
		virtual void doGetNextFrame()
		{
		}
		
		static void incomingPacketHandlerStub(void* clientData, int mask)
		{
			V4L2DeviceSource* source = (V4L2DeviceSource*) clientData;
			source->incomingPacketHandler(mask);
		}	
		
		void incomingPacketHandler(int mask) 
		{
			if (!isCurrentlyAwaitingData()) return;
			
			char buffer[m_bufferSize];
			
			struct timeb ref;
			ftime(&ref); 
			int newFrameSize = v4l2_read(m_fd, &buffer,  m_bufferSize);
			
			if (newFrameSize < 0)
			{
				envir() << "V4L2DeviceSource::incomingPacketHandler fd:"  << m_fd << " mask:" << mask << " errno:" << errno << " "  << strerror(errno) << "\n";		
			}
			else
			{
				struct timeb current;
				ftime(&current); 
				envir() << "V4L2DeviceSource::incomingPacketHandler read time:"  << int((current.time-ref.time)*1000 + current.millitm-ref.millitm) << " ms \n";		
				
				gettimeofday(&fPresentationTime, NULL);
				fDurationInMicroseconds = 0;
												
				if (newFrameSize > fMaxSize) 
				{
					fFrameSize = fMaxSize;
					fNumTruncatedBytes = newFrameSize - fMaxSize;
				} 
				else 
				{
					fFrameSize = newFrameSize;
				}
				memcpy(fTo, &buffer, fFrameSize);
	  
				afterGetting(this); 
			}
		}	

	private:
		V4L2DeviceParameters m_params;
		int m_fd;
		int m_bufferSize;
};

class MJPEGVideoSource : public JPEGVideoSource
{
 	public:
                static MJPEGVideoSource* createNew (UsageEnvironment& env, FramedSource* source)
		{
                 	return new MJPEGVideoSource(env,source);
		}

	public:
                virtual void doGetNextFrame()
		{
			if (m_inputSource)
			{
				m_inputSource->getNextFrame(fTo, fMaxSize, afterGettingFrameSub, this, FramedSource::handleClosure, this);
			}
		}

		static void afterGettingFrameSub(void* clientData, unsigned frameSize,unsigned numTruncatedBytes,struct timeval presentationTime,unsigned durationInMicroseconds) 
		{
                 		MJPEGVideoSource* source = (MJPEGVideoSource*)clientData;
   				source->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
    		}
                void afterGettingFrame(unsigned frameSize,unsigned numTruncatedBytes,struct timeval presentationTime,unsigned durationInMicroseconds)
		{			
			int headerSize = 0;
			bool headerOk = false;

			for (unsigned int i = 0; i < frameSize ; ++i) 
			{
				// SOF 
				if ( (i+8) < frameSize  && fTo[i] == 0xFF && fTo[i+1] == 0xC0 ) 
				{
					m_height = (fTo[i+5]<<5)|(fTo[i+6]>>3);
					m_width = (fTo[i+7]<<5)|(fTo[i+8]>>3);
				}
				// DQT
				if ( (i+5+64) < frameSize && fTo[i] == 0xFF && fTo[i+1] == 0xDB) 
				{
					if (fTo[i+4] ==0)
					{
						memcpy(m_qTable, fTo + i + 5, 64);
						m_qTable0Init = true;
					}
					else if (fTo[i+4] ==1)
					{
						memcpy(m_qTable + 64, fTo + i + 5, 64);
						m_qTable1Init = true;
					}
				}
				// End of header
				if ( (i+1) < frameSize && fTo[i] == 0x3F && fTo[i+1] == 0x00 ) 
				{
					headerOk = true;
					headerSize = i+2;
					break;
				}
			}

			if (headerOk)
			{
				envir() << "MJPEGVideoSource::afterGettingFrame time:" << (presentationTime.tv_sec*1000000.0+presentationTime.tv_usec)/1000000.0 
					<< " m_qTable0Init:" << m_qTable0Init << " m_qTable1Init:" << m_qTable1Init				
					<< " size:" << (m_width*8) << "x" << (m_height*8) << "\n";
				fFrameSize = frameSize - headerSize;
				memmove( fTo, fTo + headerSize, fFrameSize );
			}
			else
			{
				fFrameSize = 0;
			}

			fNumTruncatedBytes = numTruncatedBytes;
			fPresentationTime = presentationTime;
			fDurationInMicroseconds = durationInMicroseconds;
						
			afterGetting(this);
		}

		virtual u_int8_t type() { return 1; };
		virtual u_int8_t qFactor() { return 128; };
		virtual u_int8_t width() { return m_width; };
		virtual u_int8_t height() { return m_height; };
		virtual u_int8_t const* quantizationTables( u_int8_t& precision, u_int16_t& length )
		{
			length = 0;
			precision = 0;
			if ( m_qTable0Init && m_qTable1Init )
			{
				precision = 8;
				length = sizeof(m_qTable);
			}
			return m_qTable;
		}

	protected:
                MJPEGVideoSource(UsageEnvironment& env, FramedSource* source) : JPEGVideoSource(env), m_inputSource(source), m_width(0), m_height(0), m_qTable0Init(false), m_qTable1Init(false) 
		{
			memset(&m_qTable,0,sizeof(m_qTable));
		}

		virtual ~MJPEGVideoSource()
		{
			Medium::close(m_inputSource);
		}

	protected:
                FramedSource* m_inputSource;
		u_int8_t      m_width;
		u_int8_t      m_height;
		u_int8_t      m_qTable[128];
		bool          m_qTable0Init;
		bool          m_qTable1Init;
};

char quit = 0;
void sighandler(int n)
{ 
	printf("SIGINT\n");
	quit =1;
}

int main(int argc, char** argv) 
{
	// Begin by setting up our usage environment:
	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

	RTSPServer* rtspServer = RTSPServer::createNew(*env, 8554);
	if (rtspServer == NULL) 
	{
		*env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
	}
	else
	{		
		// Create RTP and RTCP groupsock:
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
		OutPacketBuffer::maxSize = 3000000;
		RTPSink* videoSink = JPEGVideoRTPSink::createNew(*env, &rtpGroupsock);

		// Create 'RTCP instance' for this RTP sink:
		const unsigned estimatedSessionBandwidth = 500; // in kbps; for RTCP b/w share
		const unsigned maxCNAMElen = 100;
		unsigned char CNAME[maxCNAMElen+1];
		gethostname((char*)CNAME, maxCNAMElen);
		CNAME[maxCNAMElen] = '\0'; 
		RTCPInstance* rtcp = RTCPInstance::createNew(*env, &rtcpGroupsock,  estimatedSessionBandwidth, CNAME, videoSink, NULL);
		
		// Create Server Session
		ServerMediaSession* sms = ServerMediaSession::createNew(*env);
		sms->addSubsession(PassiveServerMediaSubsession::createNew(*videoSink, rtcp));
		rtspServer->addServerMediaSession(sms);

		char* url = rtspServer->rtspURL(sms);
		*env << "Play this stream using the URL \"" << url << "\"\n";
		delete[] url;

		// Start the streaming:
		char                            *dev_name = "/dev/video0";	
		if (argc ==2) dev_name=argv[1];
		*env << "Create V4L2 Source..." << dev_name << "\n";
		V4L2DeviceSource::V4L2DeviceParameters param(dev_name);
		V4L2DeviceSource* videoES = V4L2DeviceSource::createNew(*env, param);
		if (videoES == NULL) 
		{
			*env << "Unable to create source";
			exit(1);
		}

		// Create a framer for the Video Elementary Stream:
		*env << "Create MJPEG Source...\n";
		MJPEGVideoSource* videoSource = MJPEGVideoSource::createNew(*env, videoES);

		// Finally, start playing:
		*env << "Starting...\n";
		videoSink->startPlaying(*videoSource, NULL, videoSink);

		signal(SIGINT,sighandler);
		env->taskScheduler().doEventLoop(&quit); 
		*env << "Exiting..\n";

		Medium::close(videoSink);
		Medium::close(rtspServer);
	}
	
	env->reclaim();
	delete scheduler;
	
	
	return 0;
}

