/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** v4l2web.cpp
** 
** -------------------------------------------------------------------------*/

#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include <json/json.h>

#include "mongoose.h"

#include "V4l2MMAPDeviceSource.h"

struct image 
{
	image(char* buffer, ssize_t length) : m_buffer(buffer), m_length(length) {}
	char* m_buffer;
	ssize_t m_length;
};

static int iterate_callback(struct mg_connection *conn, enum mg_event ) 
{
	image* img = (image*)conn->callback_param;
	if (conn->is_websocket) 
	{
		mg_websocket_write(conn, WEBSOCKET_OPCODE_BINARY, img->m_buffer, img->m_length);
	}
	else if (conn->uri && strcmp(conn->uri,"jpeg") == 0)		
	{
		fprintf(stderr, "mjpeg\n");
		mg_printf(conn, "--myboundary\r\nContent-Type: image/jpeg\r\n"
			"Content-Length: %llu\r\n\r\n", (unsigned long long) img->m_length);
		mg_write(conn, img->m_buffer, img->m_length);
		mg_write(conn, "\r\n", 2);
	}
    
	return MG_TRUE;
}

unsigned int add_ctrl(int fd, unsigned int i, Json::Value & json) 
{
	unsigned int ret=0;
	struct v4l2_queryctrl   qctrl;
	memset(&qctrl,0,sizeof(qctrl));
	qctrl.id = i;
	if (0 == ioctl(fd,VIDIOC_QUERYCTRL,&qctrl))
	{
		if (!(qctrl.flags & V4L2_CTRL_FLAG_DISABLED))
		{
			struct v4l2_control control;  
			memset(&control,0,sizeof(control));
			control.id = qctrl.id;
			if (0 == ioctl(fd,VIDIOC_G_CTRL,&control))
			{
				Json::Value value;
				value["id"] = i;
				value["name"] = (const char*)qctrl.name;
				value["type"] = qctrl.type;
				value["minimum"] = qctrl.minimum;
				value["maximum"] = qctrl.maximum;
				value["step"] = qctrl.step;
				value["default_value"] = qctrl.default_value;
				value["value"] = control.value;

				Json::Value flags;
				if (qctrl.flags & V4L2_CTRL_FLAG_DISABLED) flags.append("V4L2_CTRL_FLAG_DISABLED");
				if (qctrl.flags & V4L2_CTRL_FLAG_GRABBED) flags.append("V4L2_CTRL_FLAG_GRABBED");
				if (qctrl.flags & V4L2_CTRL_FLAG_READ_ONLY) flags.append("V4L2_CTRL_FLAG_READ_ONLY");
				if (qctrl.flags & V4L2_CTRL_FLAG_UPDATE) flags.append("V4L2_CTRL_FLAG_UPDATE");
				if (qctrl.flags & V4L2_CTRL_FLAG_SLIDER) flags.append("V4L2_CTRL_FLAG_SLIDER");
				if (qctrl.flags & V4L2_CTRL_FLAG_WRITE_ONLY) flags.append("V4L2_CTRL_FLAG_WRITE_ONLY");
				value["flags"]   = flags;
				
				if ( (qctrl.type == V4L2_CTRL_TYPE_MENU) || (qctrl.type == V4L2_CTRL_TYPE_INTEGER_MENU) )
				{
					Json::Value menu;
					struct v4l2_querymenu querymenu;
					memset(&querymenu,0,sizeof(querymenu));
					querymenu.id = qctrl.id;
					for (querymenu.index = 0; querymenu.index <= qctrl.maximum; querymenu.index++) 
					{
						if (0 == ioctl(fd,VIDIOC_QUERYMENU,&querymenu))
						{
							Json::Value label;
							label["value"] = querymenu.index;
							if (qctrl.type == V4L2_CTRL_TYPE_MENU)
							{
								label["label"] = (const char*)querymenu.name;
							}
							else if (qctrl.type == V4L2_CTRL_TYPE_INTEGER_MENU)
							{
								label["label"] = (int)querymenu.value;
							}
							menu.append(label);
						}
					}
					value["menu"] = menu;
				}
				json.append(value);
			}
		}
		ret = qctrl.id;
	}
	return ret;
}

std::string get_fourcc(unsigned int pixelformat)
{
	std::string fourcc;
	fourcc.append(1, pixelformat&0xff);
	fourcc.append(1, (pixelformat>>8)&0xff);
	fourcc.append(1, (pixelformat>>16)&0xff);
	fourcc.append(1, (pixelformat>>24)&0xff);
	return fourcc;
}

static int send_capabilities_reply(struct mg_connection *conn) 
{
	V4L2Device* dev =(V4L2Device*)conn->server_param;
	int fd = dev->getFd();		
	Json::Value json;
	v4l2_capability cap;
	memset(&cap,0,sizeof(cap));
	if (-1 != ioctl(fd,VIDIOC_QUERYCAP,&cap))
	{
		json["driver"]     = (const char*)cap.driver;
		json["card"]       = (const char*)cap.card;
		json["bus_info"]   = (const char*)cap.bus_info;
		Json::Value capabilities;
		if (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) capabilities.append("V4L2_CAP_VIDEO_CAPTURE");
		if (cap.device_caps & V4L2_CAP_VIDEO_OUTPUT) capabilities.append("V4L2_CAP_VIDEO_OUTPUT");
		if (cap.device_caps & V4L2_CAP_READWRITE) capabilities.append("V4L2_CAP_READWRITE");
		if (cap.device_caps & V4L2_CAP_ASYNCIO) capabilities.append("V4L2_CAP_ASYNCIO");
		if (cap.device_caps & V4L2_CAP_STREAMING) capabilities.append("V4L2_CAP_STREAMING");
		json["capabilities"]   = capabilities;
	}
	Json::StyledWriter styledWriter;
	std::string str (styledWriter.write(json));
	mg_printf_data(conn, str.c_str());		
	return MG_TRUE;
}

static int send_inputs_reply(struct mg_connection *conn) 
{
	V4L2Device* dev =(V4L2Device*)conn->server_param;
	int fd = dev->getFd();		
	Json::Value json;
	for (int i = 0;; i++) 
	{
		v4l2_input input;
		memset(&input,0,sizeof(input));
		input.index = i;
		if (-1 == ioctl(fd,VIDIOC_ENUMINPUT,&input))
			break;
		
		Json::Value value;
		value["name"]   = (const char*)input.name;
		value["type"]   = input.type;
		value["status"] = input.status;			
		json.append(value);
	}
	Json::StyledWriter styledWriter;
	std::string str (styledWriter.write(json));
	mg_printf_data(conn, str.c_str());		
	return MG_TRUE;
}

void add_frameIntervals(int fd, unsigned int pixelformat, unsigned int width, unsigned int height, Json::Value & frameSize) 
{
	Json::Value frameIntervals;
	struct v4l2_frmivalenum frmival;
	frmival.index = 0;
	frmival.pixel_format = pixelformat;
	frmival.width = width;
	frmival.height = height;
	while (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0) 
	{
		Json::Value frameInter;
		if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) 
		{
			frameInter["fps"] = 1.0*frmival.discrete.numerator/frmival.discrete.denominator;
		}
		else
		{
			frameInter["min_fps"] = 1.0*frmival.stepwise.min.numerator/frmival.stepwise.min.denominator;
			frameInter["max_fps"] = 1.0*frmival.stepwise.max.numerator/frmival.stepwise.max.denominator;
		}
		frameIntervals.append(frameInter);
		frmival.index++;
	}
	frameSize["intervals"] = frameIntervals;
}
				
static int send_formats_reply(struct mg_connection *conn) 
{
	V4L2Device* dev =(V4L2Device*)conn->server_param;
	int fd = dev->getFd();		
	Json::Value json;
	for (int i = 0;; i++) 
	{
		struct v4l2_fmtdesc     fmtdesc;
		memset(&fmtdesc,0,sizeof(fmtdesc));
		fmtdesc.index = i;
		fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (-1 == ioctl(fd,VIDIOC_ENUM_FMT,&fmtdesc))
			break;
		
		Json::Value value;
		value["description"] = (const char*)fmtdesc.description;
		value["type"]        = fmtdesc.type;
		value["format"]      = get_fourcc(fmtdesc.pixelformat);		

		Json::Value frameSizeList;
		struct v4l2_frmsizeenum frmsize;
		memset(&frmsize,0,sizeof(frmsize));
		frmsize.pixel_format = fmtdesc.pixelformat;
		frmsize.index = 0;
		while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) 
		{
			Json::Value frameSize;
			if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) 
			{				
				frameSize["width"] = frmsize.discrete.width;
				frameSize["height"] = frmsize.discrete.height;
				
				add_frameIntervals(fd, frmsize.pixel_format, frmsize.discrete.width, frmsize.discrete.height, frameSize);
			}
			else 
			{
				frameSize["min_width"] = frmsize.stepwise.min_width;
				frameSize["min_height"] = frmsize.stepwise.min_height;
				frameSize["max_width"] = frmsize.stepwise.max_width;
				frameSize["max_height"] = frmsize.stepwise.max_height;
				frameSize["step_width"] = frmsize.stepwise.step_width;
				frameSize["step_height"] = frmsize.stepwise.step_height;

				add_frameIntervals(fd, frmsize.pixel_format, frmsize.stepwise.max_width, frmsize.stepwise.max_height, frameSize);				
			}
			frameSizeList.append(frameSize);
			frmsize.index++;
		}
		value["frameSizes"]     = frameSizeList;

		json.append(value);
	}
	Json::StyledWriter styledWriter;
	std::string str (styledWriter.write(json));
	mg_printf_data(conn, str.c_str());		
	return MG_TRUE;
}

static int send_controls_reply(struct mg_connection *conn) 
{
	V4L2Device* dev =(V4L2Device*)conn->server_param;
	int fd = dev->getFd();		
	Json::Value json;
	if (conn->query_string == NULL)
	{
		for (unsigned int i = V4L2_CID_BASE; i<V4L2_CID_LASTP1; add_ctrl(fd,i,json), i++);
		for (unsigned int i = V4L2_CID_PRIVATE_BASE; add_ctrl(fd,i,json) != 0 ; i++);
		for (unsigned int i = V4L2_CTRL_FLAG_NEXT_CTRL; i != 0 ; i=add_ctrl(fd,i|V4L2_CTRL_FLAG_NEXT_CTRL,json));
	}
	else
	{
		char * query = strdup(conn->query_string);
		char * key = strtok(query, "=");
		if (key == query)
		{
			char * value = strtok(NULL, "=");
			if (value == NULL)
			{
				add_ctrl(fd,strtol(key, NULL, 10),json);
			}
			else
			{
				struct v4l2_control control;  
				memset(&control,0,sizeof(control));
				control.id = strtol(key, NULL, 10);
				control.value = strtol(value, NULL, 10);
				errno=0;
				json["ioctl"] = ioctl(fd,VIDIOC_S_CTRL,&control);
				json["errno"]  = errno;
				json["error"]  = strerror(errno);
			}
		}
		free(query);
	}
	Json::StyledWriter styledWriter;
	std::string str (styledWriter.write(json));
	mg_printf_data(conn, str.c_str());		
	return MG_TRUE;
}

static int send_format_reply(struct mg_connection *conn) 
{
	V4L2Device* dev =(V4L2Device*)conn->server_param;
	int fd = dev->getFd();		
	Json::Value output;
	
	// set format POST
	if ( (conn->content_len != 0) && (conn->content != NULL) )
	{
		std::string content(conn->content, conn->content_len);
		Json::Value input;
		Json::Reader reader;
		reader.parse(content,input);
		
		Json::StyledWriter writer;
		std::cout << writer.write(input) << std::endl;		
		
		struct v4l2_format     format;
		memset(&format,0,sizeof(format));
		format.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (0 == ioctl(fd,VIDIOC_G_FMT,&format))
		{
			format.fmt.pix.width = input.get("width",format.fmt.pix.width).asUInt();
			format.fmt.pix.height = input.get("height",format.fmt.pix.height).asUInt();
			std::string formatstr = input.get("format","").asString();
			if (!formatstr.empty())
			{
				const char* fourcc[4];
				memset(&fourcc,0, sizeof(fourcc));
				unsigned len = sizeof(format);
				if (formatstr.size()<len) len = formatstr.size();
				memcpy(&fourcc,formatstr.c_str(),len);
				format.fmt.pix.pixelformat = v4l2_fourcc(fourcc[0],fourcc[1],fourcc[2],fourcc[3]);
			}
			errno=0;
			output["ioctl"] = ioctl(fd,VIDIOC_S_FMT,&format);
			output["errno"]  = errno;
			output["error"]  = strerror(errno);								
		}		
	}

	// query the format
	struct v4l2_format     format;
	memset(&format,0,sizeof(format));
	format.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (0 == ioctl(fd,VIDIOC_G_FMT,&format))
	{
		output["width"]     = format.fmt.pix.width;
		output["height"]    = format.fmt.pix.height;
		output["sizeimage"] = format.fmt.pix.sizeimage;
		output["format"]    = get_fourcc(format.fmt.pix.pixelformat);			
		
	}
	
	Json::StyledWriter styledWriter;
	std::string str (styledWriter.write(output));
	std::cerr << str << std::endl;	
	mg_send_header(conn,"Pragma","no-cache");
	mg_send_header(conn,"Cache-Control:","no-cache, no-store");
	mg_printf_data(conn, str.c_str());
	return MG_TRUE;
}

static int send_reply(struct mg_connection *conn) 
{
	V4L2Device* dev =(V4L2Device*)conn->server_param;
	if (conn->is_websocket) 
	{			
		if (conn->content_len == 4 && !memcmp(conn->content, "exit", 4))
		{
			mg_websocket_write(conn, WEBSOCKET_OPCODE_CONNECTION_CLOSE, conn->content, conn->content_len);
		}
		else
		{
			mg_websocket_write(conn, WEBSOCKET_OPCODE_TEXT, conn->content, conn->content_len);
		}
		return MG_TRUE;
	} 
	else if (strcmp(conn->uri,"/capabilities") ==0)
	{
		return send_capabilities_reply(conn);
	}
	else if (strcmp(conn->uri,"/inputs") ==0)
	{
		return send_inputs_reply(conn);
	}
	else if (strcmp(conn->uri,"/formats") ==0)
	{	
		return send_formats_reply(conn);
	}
	else if (strcmp(conn->uri,"/controls") ==0)
	{	
		return send_controls_reply(conn);
	}	
	else if (strcmp(conn->uri,"/format") ==0)
	{	
		return send_format_reply(conn);
	}
	else if (strcmp(conn->uri,"/start") ==0)
	{	
		dev->captureStart();
		return MG_TRUE;
	}
	else if (strcmp(conn->uri,"/stop") ==0)
	{	
		dev->captureStop();
		return MG_TRUE;
	}
	else if (strcmp(conn->uri,"/jpeg") ==0)
	{	
		mg_printf(conn, "%s",
		      "HTTP/1.0 200 OK\r\n" "Cache-Control: no-cache\r\n"
		      "Pragma: no-cache\r\nExpires: Thu, 01 Dec 1994 16:00:00 GMT\r\n"
		      "Connection: close\r\nContent-Type: multipart/x-mixed-replace; "
		      "boundary=--myboundary\r\n\r\n");	
				
		return MG_TRUE;
	}
	
	return MG_FALSE;
}

static int ev_handler(struct mg_connection *conn, enum mg_event ev) 
{
	if (ev == MG_REQUEST) 
	{
		return send_reply(conn);
	} 
	else if (ev == MG_CLOSE) 
	{
		return MG_TRUE;
	}	
	else if (ev == MG_AUTH) 
	{
		return MG_TRUE;
	} 
	else 
	{
		return MG_FALSE;
	}
}

int main(int argc, char* argv[]) 
{	
	int verbose=0;
	const char *dev_name = "/dev/video0";	
	int width = 640;
	int height = 480;	
	int c = 0;     
	while ((c = getopt (argc, argv, "hW:H:Q:P:F:v::O:T:mM")) != -1)
	{
		switch (c)
		{
			case 'v':	verbose = 1; if (optarg && *optarg=='v') verbose++;  break;
			case 'W':	width = atoi(optarg); break;
			case 'H':	height = atoi(optarg); break;
			case 'h':
			{
				std::cout << argv[0] << " [-v[v]] [-W width] [-H height] [device]" << std::endl;
				std::cout << "\t -v       : verbose " << std::endl;
				std::cout << "\t -v v     : very verbose " << std::endl;
				std::cout << "\t -W width : V4L2 capture width (default "<< width << ")" << std::endl;
				std::cout << "\t -H height: V4L2 capture height (default "<< height << ")" << std::endl;
				std::cout << "\t device   : V4L2 capture device (default "<< dev_name << ")" << std::endl;
				exit(0);
			}
		}
	}
	if (optind<argc)
	{
		dev_name = argv[optind];
	}	
	
	V4L2DeviceParameters param(dev_name,V4L2_PIX_FMT_JPEG,width,height,25,verbose);
	V4L2MMAPDeviceSource* videoCapture = V4L2MMAPDeviceSource::createNew(param);
	if (videoCapture)
	{	
		struct mg_server *server = mg_create_server(videoCapture, ev_handler);
		mg_set_option(server, "listening_port", "8080");
		std::string currentPath(get_current_dir_name());
		currentPath += "/webroot";
		mg_set_option(server, "document_root", currentPath.c_str());
		
		chdir(mg_get_option(server, "document_root"));
		printf("Started on port %s root:%s\n", mg_get_option(server, "listening_port"), mg_get_option(server, "document_root"));	
		for (;;) 
		{
			mg_poll_server(server, 10);
			if (videoCapture->isRunning())
			{
				int fd = videoCapture->getFd();
				struct timeval tv;
				timerclear(&tv);
				fd_set read_set;
				FD_ZERO(&read_set);
				FD_SET(fd,&read_set);
				if (select(fd+1, &read_set, NULL, NULL, &tv) >0)
				{
					if (FD_ISSET(fd,&read_set))
					{
						char buf[videoCapture->getBufferSize()];
						ssize_t size = videoCapture->read(buf, sizeof(buf));
						if (verbose)
						{
							fprintf(stderr, "read size:%d\n", size);
						}
						if (size>0)
						{
							image img(buf, size);
							mg_iterate_over_connections(server, iterate_callback, &img);
						}
					}
				}
			}
		}
		mg_destroy_server(&server);
		
		delete videoCapture;
	}
	
	return 0;
}
