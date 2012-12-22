#!/usr/bin/python

from gobject import MainLoop
import pygst
pygst.require("0.10")
from gst import parse_launch, MESSAGE_EOS, MESSAGE_ERROR, STATE_PAUSED, STATE_READY, STATE_NULL, STATE_PLAYING
from gst.rtspserver import Server, MediaFactory
from time import sleep
import socket


class RTSPserver:
	"""
	GStreamer RTSP server.
	"""
	def __init__(self, port, bitrate, framerate):
		"""
		**On init:** Some initialization code.
		
		:param integer port: RTSP server port.
		:param integer bitrate: Video bitrate.
		:param integer framerate: Video framerate.
		"""
		self.bitrate = bitrate
		self.framerate = framerate
		self.server = Server()
		self.loop = None
		self.server.set_service(str(port))
		self.factory = []
		self.__addMedia()
	
	def __addFactory(self,url,factory):
		mmap = self.server.get_media_mapping()
		self.factory.append(factory)
		mmap.add_factory(url,factory)
		self.server.set_media_mapping(mmap)
		print "Add Service rtsp://"+socket.gethostname()+":" + self.server.get_service() + url

	def __addMedia(self):
		"""
		videotest
		"""                                
		launch = "videotestsrc pattern=ball ! timeoverlay halign=right valign=top ! clockoverlay halign=left valign=top time-format=\"%Y/%m/%d %H:%M:%S\" ! "
		launch += "x264enc bitrate="+str(self.bitrate)+" ! rtph264pay name=pay0"                
		mfactory = MediaFactory()
		mfactory.set_launch(launch)
		mfactory.set_shared(True)
		mfactory.set_eos_shutdown(True)
		self.__addFactory("/videotest.h264", mfactory)

		"""
		webcam
		"""
		launch = "v4l2src ! video/x-raw-yuv,width=320,height=240,depth=32,framerate="+str(self.framerate)+"/1 ! timeoverlay halign=right valign=top ! clockoverlay halign=left valign=top time-format=\"%Y/%m/%d %H:%M:%S\" ! queue ! "
		launch += "x264enc bitrate="+str(self.bitrate)+" ! rtph264pay name=pay0"                
		mfactory = MediaFactory()
		mfactory.set_launch(launch)
		mfactory.set_shared(True)
		mfactory.set_eos_shutdown(True)
		self.__addFactory("/v4l2.h264", mfactory)
		
	
	def run(self):
		"""
		Attach server and run the loop (see :attr:`loop`).
		"""
		if self.server.attach():
			self.loop = MainLoop()
			self.loop.run()
	
	def stop(self):
		self.loop.quit()

srv=RTSPserver(8554,500,5);
srv.run();
