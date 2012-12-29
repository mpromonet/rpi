#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm_host.h"
#include "ilclient.h"

void printStats(COMPONENT_T * component, int port)
{
	OMX_CONFIG_BRCMPORTSTATSTYPE stats;
	memset(&stats, 0, sizeof(stats));
	stats.nSize = sizeof(stats);
	stats.nVersion.nVersion = OMX_VERSION;
	stats.nPortIndex = port;
	
	if (OMX_GetParameter(ILC_GET_HANDLE(component), OMX_IndexConfigBrcmPortStats, &stats) == OMX_ErrorNone)
	{
		printf("===========STATS ===(port:%d)====\n",port);
		printf(".nImageCount:%d\n", stats.nImageCount);
		printf(".nBufferCount:%d\n", stats.nBufferCount);
		printf(".nFrameCount:%d\n", stats.nFrameCount);
		printf(".nFrameSkips:%d\n", stats.nFrameSkips);
		printf(".nDiscards:%d\n", stats.nDiscards);
		printf(".nEOS:%d\n", stats.nEOS);
		printf(".nMaxFrameSize:%d\n", stats.nMaxFrameSize);
		printf(".nByteCount:%d\n", stats.nByteCount);
		printf(".nMaxTimeDelta:%d\n", stats.nMaxTimeDelta);
		printf(".nCorruptMBs:%d\n", stats.nCorruptMBs);
		printf("======================\n");
	}
}

static int video_source_test()
{
   OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
   COMPONENT_T *video_source = NULL, *video_scheduler = NULL, *video_render = NULL, *clock = NULL;
   COMPONENT_T *list[5];
   TUNNEL_T tunnel[4];
   ILCLIENT_T *client = NULL;
   int status = 0;

   memset(list, 0, sizeof(list));
   memset(tunnel, 0, sizeof(tunnel));

   if((client = ilclient_init()) == NULL)
   {
      return -3;
   }

   if(OMX_Init() != OMX_ErrorNone)
   {
      ilclient_destroy(client);
      return -4;
   }

   // create video_source
   if(ilclient_create_component(client, &video_source, "source", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[0] = video_source;

   // create video_render
   if(status == 0 && ilclient_create_component(client, &video_render, "video_render", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[1] = video_render;

   // setup tunnel
   set_tunnel(tunnel, video_source, 20, video_render, 90);

   // setup source
   OMX_PARAM_SOURCETYPE src;
   memset(&src, 0, sizeof(src));
   src.nSize = sizeof(src);
   src.nVersion.nVersion = OMX_VERSION;
   src.eType = 2;
   src.nFrameCount = 100;
   src.xFrameRate = 25;
   if(status == 0 && OMX_SetParameter(ILC_GET_HANDLE(video_source), OMX_IndexParamSource, &src) == OMX_ErrorNone)
	   status= -18;
   
   OMX_VIDEO_PARAM_PORTFORMATTYPE format;
   memset(&format, 0, sizeof(format));
   format.nSize = sizeof(format);
   format.nVersion.nVersion = OMX_VERSION;
   format.nPortIndex = 20;
   format.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;
   format.eCompressionFormat = OMX_IMAGE_CodingUnused;

   if(status == 0 && OMX_SetParameter(ILC_GET_HANDLE(video_source), OMX_IndexParamVideoPortFormat, &format) == OMX_ErrorNone)
	status= -16;

   printf("===========SOURCE CONFIGURED =======\n");
   	      
   if(status == 0)
   {
	printf("===========RUNNING=======\n");
	if(ilclient_setup_tunnel(tunnel, 0, 0) != 0)
	{
		status = -7;
	}
	   
	ilclient_change_component_state(video_source, OMX_StateExecuting);            
	ilclient_change_component_state(video_render, OMX_StateExecuting);
	    
	sleep(5);
	printStats(video_source,20);
	printStats(video_render,90);
	   
	printf("===========STOPPING=======\n");
      
	// wait for EOS from render
	ilclient_wait_for_event(video_render, OMX_EventBufferFlag, 90, 0, OMX_BUFFERFLAG_EOS, 0, ILCLIENT_BUFFER_FLAG_EOS, 10000);

	printf("===========FLUSHING=======\n");

	// need to flush the renderer to allow video_source to disable its input port
	ilclient_flush_tunnels(tunnel, 0);
   }
   
   printf("===========CLOSING=======\n");
   
   ilclient_disable_tunnel(tunnel);
   ilclient_teardown_tunnels(tunnel);

   ilclient_state_transition(list, OMX_StateIdle);
   ilclient_state_transition(list, OMX_StateLoaded);

   ilclient_cleanup_components(list);

   OMX_Deinit();

   ilclient_destroy(client);
   
   printf("status:%d\n",status);
   
   return status;
}

int main (int argc, char **argv)
{
   bcm_host_init();
   return video_source_test();
}


