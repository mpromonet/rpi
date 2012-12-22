#include <stdio.h>
#include <stdlib.h>


#include "bcm_host.h"
#include "interface/vcos/vcos_logging.h"
#include "ilclient.h"

int main(int argc, char **argv)
{
	bcm_host_init();

	ILCLIENT_T *client = ilclient_init();
	if (client == NULL)
	{
		printf("ilclient_init failed");
		return -1;		
	}	
	OMX_ERRORTYPE err = OMX_Init();
	if (err != OMX_ErrorNone) 
	{	
		printf("omx_init failed err:%X!\n",err);
		return -1;
	}	
	
	COMPONENT_T *camera = NULL;
	err = ilclient_create_component(client, &camera, "camera", 0);
	if (err != OMX_ErrorNone) 
	{
		printf("%s:%d: ilclient_create_component failed err:%X!\n",__FUNCTION__, __LINE__, err);
		return -1;
	}	
	OMX_PARAM_U32TYPE def;
	err = OMX_GetParameter(ILC_GET_HANDLE(camera), OMX_IndexParamCameraDevicesPresent,&def);
	if (err != OMX_ErrorNone) 
	{
		printf("%s:%d: OMX_GetParameter() failed err:%X!!\n",__FUNCTION__, __LINE__,err);
		return -1;
	}
	OMX_Deinit();
	
    ilclient_destroy(client);
	
	return 0;
}
