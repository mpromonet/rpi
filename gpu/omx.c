#include <stdio.h>
#include <stdlib.h>

#include <IL/OMX_Core.h>

int main(int argc, char **argv)
{
    bcm_host_init();
    OMX_Init();

    char name[OMX_MAX_STRINGNAME_SIZE];
    name[0] = '\0';

    OMX_U32 index = 0;
    OMX_ERRORTYPE err = OMX_ErrorNone;
    do
    {
        err = OMX_ComponentNameEnum(name, OMX_MAX_STRINGNAME_SIZE, index);
        if (err == OMX_ErrorNone)
        {
		OMX_U32 numRoles = 0;
		err = OMX_GetRolesOfComponent(name,&numRoles,NULL);
		if (err != OMX_ErrorNone)
		{
			printf("Error er:%x\n", err);
		}
		printf("#%d [%s] numRoles = %x\n", index, name, numRoles);
		index++;
        }
    } while (err == OMX_ErrorNone);
   
    OMX_Deinit();

    return 0;
}
