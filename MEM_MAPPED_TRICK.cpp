#include "stdafx.h"
#include "windows.h"
#include "stdio.h"


#pragma comment(lib,"ntdll")
#define MEM_AT_PAGE_ROUND 0x40000000
#define ViewUnmap 2
#define MemoryBasicInformation 0
#define MemoryBasicVlmInformation 0x3
#define OBJ_CASE_INSENSITIVE 0x00000040
#define DIRECTORY_QUERY (0x0001)
#define DIRECTORY_TRAVERSE (0x0002)
#define DIRECTORY_CREATE_OBJECT (0x0004)
#define DIRECTORY_CREATE_SUBDIRECTORY (0x0008)
#define DIRECTORY_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED|0xF)

struct MEMORY_BASIC_VLM_INFORMATION
{
	unsigned long ImageBase;
	unsigned long blah[0x2];
	unsigned long SizeOfImage;
};
struct UNICODE_S
{
	unsigned short len;
	unsigned short max;
	wchar_t* pStr;
};


struct OBJECT_ATTRIBUTES
{
  unsigned long           Length;
  HANDLE                  RootDirectory;
  UNICODE_S*              ObjectName;
  unsigned long           Attributes;
  void*           SecurityDescriptor;
  void*           SecurityQualityOfService;
};



extern "C"
{
	int __stdcall ZwOpenDirectoryObject(HANDLE* ObjectHandle,unsigned long DesiredAccess,OBJECT_ATTRIBUTES* ObjectAttributes);

	int __stdcall ZwOpenSection(HANDLE* SectionHandle,unsigned long DesiredAccess,OBJECT_ATTRIBUTES* ObjectAttributes);

	int __stdcall ZwCreateSection(HANDLE* SectionHandle,unsigned long DesiredAccess,OBJECT_ATTRIBUTES* ObjectAttributes,
		                          LARGE_INTEGER* MaximumSize,unsigned long SectionPageProtection,unsigned long AllocationAttributes,
								  HANDLE FileHandle);

	int __stdcall ZwMapViewOfSection(HANDLE SectionHandle,HANDLE ProcessHandle,
		                             unsigned long* BaseAddress,unsigned long ZeroBits,
									 unsigned long CommitSize,LARGE_INTEGER* pSectionOffset,
									 unsigned long* pViewSize,unsigned long InheritDisposition,
									 unsigned long AllocationType,unsigned long Win32Protection);

	int __stdcall ZwQueryVirtualMemory(HANDLE hProcess,void* BaseAddress,unsigned long InfClass,void* MemoryInformation,
		                               unsigned long MemoryInformationLength,unsigned long* pResultLength);

	int __stdcall ZwReadVirtualMemory(HANDLE hProcess,void* BaseAddress,void* Buffer,unsigned long Length,unsigned long* BytesRead);
	int __stdcall ZwUnmapViewOfSection(HANDLE hProcess,void* BaseAddress);
}

struct RegionInfo
{
   unsigned long Offset;//Offset within
   unsigned long Size;  //RegionSize
   unsigned long Prot;  //Protection
};

void MainFunction()
{
	char Arr[7]={'w','a','l','i','e','d',0};
	MessageBox(0,Arr,Arr,0);
	ExitProcess(0);
}

int main()
{
	//N.B. AT_PAGE_ROUND is not supported for Wow64 Proceses.
	HANDLE hProcess=GetCurrentProcess();
	unsigned char* hUser32=(unsigned char*)GetModuleHandle("user32.dll");
	//--------------Find SizeOfImage of user32.dll----------------------------------------------------------
	MEMORY_BASIC_VLM_INFORMATION MBVLMI={0};
	int ret=ZwQueryVirtualMemory(hProcess,hUser32,MemoryBasicVlmInformation,
		                         &MBVLMI,sizeof(MEMORY_BASIC_VLM_INFORMATION),0);
	if(ret<0)
	{
		printf("ZwQueryVirtualMemory Error: %x\r\n",ret);
		return -1;
	}
	unsigned long SizeOfImage=0;
	if(MBVLMI.ImageBase==(unsigned long)hUser32) SizeOfImage=MBVLMI.SizeOfImage;
	if(SizeOfImage==0) return -1;
	//-------------------Store Protection value of each user32.dll on heap-----------------------------------
	RegionInfo* pRegionInfo=(RegionInfo*)VirtualAlloc(0,0x3000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
	unsigned long iRegion=0;
	unsigned long LastRegionSize=0;
	MEMORY_BASIC_INFORMATION MBI={0};

	for(unsigned long c=0;c<SizeOfImage;c+=LastRegionSize,iRegion++)
	{
		memset(&MBI,0,sizeof(MBI));
		ret=ZwQueryVirtualMemory(hProcess,hUser32+c,MemoryBasicInformation,
		                         &MBI,sizeof(MEMORY_BASIC_INFORMATION),0);

		
		if(iRegion<0x400)
		{
			if(ret>=0)
			{
				pRegionInfo[iRegion].Offset=c;
				pRegionInfo[iRegion].Prot=MBI.Protect;
				pRegionInfo[iRegion].Size=MBI.RegionSize;
			}	
			else
			{
				pRegionInfo[iRegion].Offset=-1;
				pRegionInfo[iRegion].Prot=-1;
				pRegionInfo[iRegion].Size=-1;
			}
		}

		LastRegionSize=MBI.RegionSize;
	}
	//---------------Create a Section Object with Future size equal to SizeOfImage of user32.dll-------------
	HANDLE hSection=0;
	OBJECT_ATTRIBUTES ObjAttr={sizeof(OBJECT_ATTRIBUTES)};
	unsigned long MaximumSize[2]={SizeOfImage,0};
	ret=ZwCreateSection(&hSection,0xF001F,&ObjAttr,(LARGE_INTEGER*)(&MaximumSize[0]),PAGE_EXECUTE_READWRITE,SEC_COMMIT,0);
	if(ret<0)
	{
		printf("ZwCreateSection Error: %x\r\n",ret);
		return -1;
	}
	//----------------Map view of the section N.B. Writable-----------------------------------------------------
	unsigned long BaseAddressNewSection=0;
	unsigned long SectionOffset[2]={0};
	unsigned long ViewSize=0;
	ret=ZwMapViewOfSection(hSection,hProcess,&BaseAddressNewSection,0,0,(LARGE_INTEGER*)(&SectionOffset[0]),&ViewSize,ViewUnmap,
		                   0,PAGE_EXECUTE_READWRITE);
	if(ret<0)
	{
		printf("ZwMapViewOfSection Error: %x\r\n",ret);
		return -1;
	}
	//----------------Copy from user32.dll into new view-------------------------------------------
	//------Reading Page by Page is recommended------------------------------------------------------
	for(unsigned long i=0;i<SizeOfImage;i+=0x1000)
	{
		ZwReadVirtualMemory(hProcess,(void*)(hUser32+i),(void*)(BaseAddressNewSection+i),0x1000,0);
	}
	//---------------Unmap view of Original user32.dll N.B. Beware if multi-threaded---------------
	//---------------You should suspend all other threads beforehand---------------------------------
	ret=ZwUnmapViewOfSection(hProcess,hUser32);
	if(ret<0)
	{
		printf("ZwUnmapViewOfSection Error: %x\r\n",ret);
		return -1;
	}
	//--------------
	unsigned long BaseX=(unsigned long)hUser32;
	unsigned long SectionOffsetX[2]={0};
	unsigned long ViewSizeX=0x1000;

	for(unsigned long x=0;x<iRegion;x++)
	{
		unsigned long BaseXX=BaseX+pRegionInfo[x].Offset;         //Base Address at which function will map
		unsigned long ViewSizeXX=pRegionInfo[x].Size;             //How much will it map?
		unsigned long SectionOffsetXX[2]={pRegionInfo[x].Offset}; //Offset within source

		ret=ZwMapViewOfSection(hSection,hProcess,&BaseXX,0,0,(LARGE_INTEGER*)(&SectionOffsetXX[0]),
			                   &ViewSizeXX,ViewUnmap,MEM_AT_PAGE_ROUND,pRegionInfo[x].Prot);
	}
	//----------------------------
	//Now Try to place any Software/INT3 Breakpoint in user32.dll
	MainFunction();
	//----------------------------
	VirtualFree(pRegionInfo,0x3000,MEM_RELEASE|MEM_DECOMMIT);
	return 0;
}
