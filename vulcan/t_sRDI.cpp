// RDIShellcodeCLoader.cpp : Defines the entry point for the console application.
//

//#include "stdafx.h"
#include <windows.h>
#include <string>
#include <iostream>
#include "auxiliary.h"

#define DEREF_64( name )*(DWORD64 *)(name)
#define DEREF_32( name )*(DWORD *)(name)
#define DEREF_16( name )*(WORD *)(name)
#define DEREF_8( name )*(BYTE *)(name)
#define ROTR32(value, shift)	(((DWORD) value >> (BYTE) shift) | ((DWORD) value << (32 - (BYTE) shift)))

#define SRDI_CLEARHEADER 0x1

FARPROC sRDIGetProcAddressR(UINT_PTR uiLibraryAddress, LPCSTR lpProcName)
{
	FARPROC fpResult = NULL;

	if (uiLibraryAddress == NULL)
		return NULL;

	UINT_PTR uiAddressArray = 0;
	UINT_PTR uiNameArray = 0;
	UINT_PTR uiNameOrdinals = 0;
	PIMAGE_NT_HEADERS pNtHeaders = NULL;
	PIMAGE_DATA_DIRECTORY pDataDirectory = NULL;
	PIMAGE_EXPORT_DIRECTORY pExportDirectory = NULL;

	// get the VA of the modules NT Header
	pNtHeaders = (PIMAGE_NT_HEADERS)(uiLibraryAddress + ((PIMAGE_DOS_HEADER)uiLibraryAddress)->e_lfanew);

	pDataDirectory = (PIMAGE_DATA_DIRECTORY)&pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

	// get the VA of the export directory
	pExportDirectory = (PIMAGE_EXPORT_DIRECTORY)(uiLibraryAddress + pDataDirectory->VirtualAddress);

	// get the VA for the array of addresses
	uiAddressArray = (uiLibraryAddress + pExportDirectory->AddressOfFunctions);

	// get the VA for the array of name pointers
	uiNameArray = (uiLibraryAddress + pExportDirectory->AddressOfNames);

	// get the VA for the array of name ordinals
	uiNameOrdinals = (uiLibraryAddress + pExportDirectory->AddressOfNameOrdinals);

	// test if we are importing by name or by ordinal...
	if (((DWORD)lpProcName & 0xFFFF0000) == 0x00000000)
	{
		// import by ordinal...

		// use the import ordinal (- export ordinal base) as an index into the array of addresses
		uiAddressArray += ((IMAGE_ORDINAL((DWORD)lpProcName) - pExportDirectory->Base) * sizeof(DWORD));

		// resolve the address for this imported function
		fpResult = (FARPROC)(uiLibraryAddress + DEREF_32(uiAddressArray));
	}
	else
	{
		// import by name...
		DWORD dwCounter = pExportDirectory->NumberOfNames;
		while (dwCounter--)
		{
			char * cpExportedFunctionName = (char *)(uiLibraryAddress + DEREF_32(uiNameArray));

			// test if we have a match...
			if (strcmp(cpExportedFunctionName, lpProcName) == 0)
			{
				// use the functions name ordinal as an index into the array of name pointers
				uiAddressArray += (DEREF_16(uiNameOrdinals) * sizeof(DWORD));

				// calculate the virtual address for the function
				fpResult = (FARPROC)(uiLibraryAddress + DEREF_32(uiAddressArray));

				// finish...
				break;
			}

			// get the next exported function name
			uiNameArray += sizeof(DWORD);

			// get the next exported function name ordinal
			uiNameOrdinals += sizeof(WORD);
		}
	}

	return fpResult;
}

BOOL Is64BitDLL(UINT_PTR uiLibraryAddress)
{
	PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)(uiLibraryAddress + ((PIMAGE_DOS_HEADER)uiLibraryAddress)->e_lfanew);

	if (pNtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) return true;
	else return false;
}

DWORD GetFileContents(LPCSTR filename, LPSTR *data, DWORD &size)
{
	std::FILE *fp = std::fopen(filename, "rb");

	if (fp)
	{
		fseek(fp, 0, SEEK_END);
		size = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		*data = (LPSTR)malloc(size + 1);
		fread(*data, size, 1, fp);
		fclose(fp);
		return true;
	}
	return false;
}

DWORD HashFunctionName(LPSTR name) {
	DWORD hash = 0;

	do
	{
		hash = ROTR32(hash, 13);
		hash += *name;
		name++;
	} while (*(name - 1) != 0);

	return hash;
}

BOOL ConvertToShellcode(LPVOID inBytes, DWORD length, DWORD userFunction, LPVOID userData, DWORD userLength, DWORD flags, LPSTR &outBytes, DWORD &outLength)
{

	LPSTR rdiShellcode = NULL;
	DWORD rdiShellcodeLength, dllOffset, userDataLocation;

#ifdef _DEBUG
	LPSTR rdiShellcode64 = NULL, rdiShellcode32 = NULL;
	DWORD rdiShellcode64Length = 0, rdiShellcode32Length = 0;
	GetFileContents("../bin/ShellcodeRDI_x64.bin", &rdiShellcode64, rdiShellcode64Length);
	GetFileContents("../bin/ShellcodeRDI_x86.bin", &rdiShellcode32, rdiShellcode32Length);

#else
	LPSTR rdiShellcode32 = "\x83\xEC\x48\x83\x64\x24\x18\x00\xB9\x4C\x77\x26\x07\x53\x55\x56\x57\x33\xF6\xE8\x5C\x04\x00\x00\xB9\x49\xF7\x02\x78\x89\x44\x24\x1C\xE8\x4E\x04\x00\x00\xB9\x58\xA4\x53\xE5\x89\x44\x24\x20\xE8\x40\x04\x00\x00\xB9\x10\xE1\x8A\xC3\x8B\xE8\xE8\x34\x04\x00\x00\xB9\xAF\xB1\x5C\x94\x89\x44\x24\x2C\xE8\x26\x04\x00\x00\xB9\x33\x00\x9E\x95\x89\x44\x24\x30\xE8\x18\x04\x00\x00\x8B\xD8\x8B\x44\x24\x5C\x8B\x78\x3C\x03\xF8\x89\x7C\x24\x14\x81\x3F\x50\x45\x00\x00\x74\x07\x33\xC0\xE9\xF2\x03\x00\x00\xB8\x4C\x01\x00\x00\x66\x39\x47\x04\x75\xEE\xF6\x47\x38\x01\x75\xE8\x0F\xB7\x57\x06\x0F\xB7\x47\x14\x85\xD2\x74\x22\x8D\x4F\x24\x03\xC8\x83\x79\x04\x00\x8B\x01\x75\x05\x03\x47\x38\xEB\x03\x03\x41\x04\x3B\xC6\x0F\x47\xF0\x83\xC1\x28\x83\xEA\x01\x75\xE3\x8D\x44\x24\x34\x50\xFF\xD3\x8B\x44\x24\x38\x8B\x5F\x50\x8D\x50\xFF\x8D\x48\xFF\xF7\xD2\x48\x03\xCE\x03\xC3\x23\xCA\x23\xC2\x3B\xC1\x75\x97\x6A\x04\xBE\x00\x30\x00\x00\x56\x53\xFF\x77\x34\xFF\xD5\x8B\xD8\x89\x5C\x24\x10\x85\xDB\x75\x0F\x6A\x04\x56\xFF\x77\x50\x50\xFF\xD5\x8B\xD8\x89\x44\x24\x10\x8B\x77\x54\x33\xC0\x8B\x6C\x24\x5C\x40\x33\xC9\x89\x44\x24\x24\x8B\xD3\x85\xF6\x74\x34\x8B\x5C\x24\x6C\x23\xD8\x4E\x85\xDB\x74\x19\x8B\xC7\x2B\x44\x24\x5C\x3B\xC8\x73\x0F\x83\xF9\x3C\x72\x05\x83\xF9\x3E\x76\x05\xC6\x02\x00\xEB\x05\x8A\x45\x00\x88\x02\x41\x45\x42\x85\xF6\x75\xD6\x8B\x5C\x24\x10\x0F\xB7\x47\x06\x0F\xB7\x4F\x14\x85\xC0\x74\x38\x83\xC7\x2C\x03\xCF\x8B\x7C\x24\x5C\x8B\x51\xF8\x48\x8B\x31\x03\xD3\x8B\x69\xFC\x03\xF7\x89\x44\x24\x5C\x85\xED\x74\x0F\x8A\x06\x88\x02\x42\x46\x83\xED\x01\x75\xF5\x8B\x44\x24\x5C\x83\xC1\x28\x85\xC0\x75\xD5\x8B\x7C\x24\x14\x8B\xB7\x80\x00\x00\x00\x03\xF3\x89\x74\x24\x18\x8B\x46\x0C\x85\xC0\x74\x7D\x03\xC3\x50\xFF\x54\x24\x20\x8B\x6E\x10\x8B\xF8\x8B\x06\x03\xEB\x03\xC3\x89\x44\x24\x5C\x83\x7D\x00\x00\x74\x4F\x8B\x74\x24\x20\x8B\x08\x85\xC9\x74\x1E\x79\x1C\x8B\x47\x3C\x0F\xB7\xC9\x8B\x44\x38\x78\x2B\x4C\x38\x10\x8B\x44\x38\x1C\x8D\x04\x88\x8B\x04\x38\x03\xC7\xEB\x0C\x8B\x45\x00\x83\xC0\x02\x03\xC3\x50\x57\xFF\xD6\x89\x45\x00\x83\xC5\x04\x8B\x44\x24\x5C\x83\xC0\x04\x89\x44\x24\x5C\x83\x7D\x00\x00\x75\xB9\x8B\x74\x24\x18\x8B\x46\x20\x83\xC6\x14\x89\x74\x24\x18\x85\xC0\x75\x87\x8B\x7C\x24\x14\x8B\xC3\x2B\x47\x34\x89\x44\x24\x1C\x0F\x84\xBB\x00\x00\x00\x83\xBF\xA4\x00\x00\x00\x00\x0F\x84\xAE\x00\x00\x00\x8B\xB7\xA0\x00\x00\x00\x03\xF3\x89\x74\x24\x5C\x8D\x4E\x04\x8B\x01\x89\x4C\x24\x18\x85\xC0\x0F\x84\x91\x00\x00\x00\x8B\x7C\x24\x1C\x8B\x16\x8D\x68\xF8\x03\xD3\x8D\x46\x08\xD1\xED\x89\x44\x24\x20\x74\x60\x6A\x02\x8B\xD8\x5E\x0F\xB7\x0B\x4D\x66\x8B\xC1\x66\xC1\xE8\x0C\x66\x83\xF8\x0A\x74\x06\x66\x83\xF8\x03\x75\x0B\x81\xE1\xFF\x0F\x00\x00\x01\x3C\x11\xEB\x27\x66\x3B\x44\x24\x24\x75\x11\x81\xE1\xFF\x0F\x00\x00\x8B\xC7\xC1\xE8\x10\x66\x01\x04\x11\xEB\x0F\x66\x3B\xC6\x75\x0A\x81\xE1\xFF\x0F\x00\x00\x66\x01\x3C\x11\x03\xDE\x85\xED\x75\xB1\x8B\x5C\x24\x10\x8B\x74\x24\x5C\x8B\x4C\x24\x18\x03\x31\x89\x74\x24\x5C\x8D\x4E\x04\x8B\x01\x89\x4C\x24\x18\x85\xC0\x0F\x85\x77\xFF\xFF\xFF\x8B\x7C\x24\x14\x0F\xB7\x47\x06\x0F\xB7\x4F\x14\x85\xC0\x0F\x84\xB7\x00\x00\x00\x8B\x74\x24\x5C\x8D\x6F\x3C\x03\xE9\x48\x83\x7D\xEC\x00\x89\x44\x24\x24\x0F\x86\x94\x00\x00\x00\x8B\x4D\x00\x33\xD2\x42\x8B\xC1\xC1\xE8\x1D\x23\xC2\x8B\xD1\xC1\xEA\x1E\x83\xE2\x01\xC1\xE9\x1F\x85\xC0\x75\x18\x85\xD2\x75\x07\x6A\x08\x5E\x6A\x01\xEB\x05\x6A\x04\x5E\x6A\x02\x85\xC9\x58\x0F\x44\xF0\xEB\x2C\x85\xD2\x75\x17\x85\xC9\x75\x04\x6A\x10\xEB\x15\x85\xD2\x75\x0B\x85\xC9\x74\x18\xBE\x80\x00\x00\x00\xEB\x11\x85\xC9\x75\x05\x6A\x20\x5E\xEB\x08\x6A\x40\x85\xC9\x58\x0F\x45\xF0\x8B\x4D\x00\x8B\xC6\x0D\x00\x02\x00\x00\x81\xE1\x00\x00\x00\x04\x0F\x44\xC6\x8B\xF0\x8D\x44\x24\x28\x50\x8B\x45\xE8\x56\xFF\x75\xEC\x03\xC3\x50\xFF\x54\x24\x3C\x85\xC0\x0F\x84\xD0\xFC\xFF\xFF\x8B\x44\x24\x24\x83\xC5\x28\x85\xC0\x0F\x85\x52\xFF\xFF\xFF\x8B\x77\x28\x6A\x00\x6A\x00\x6A\xFF\x03\xF3\xFF\x54\x24\x3C\x33\xC0\x40\x50\x50\x53\xFF\xD6\x83\x7C\x24\x60\x00\x74\x7C\x83\x7F\x7C\x00\x74\x76\x8B\x4F\x78\x03\xCB\x8B\x41\x18\x85\xC0\x74\x6A\x83\x79\x14\x00\x74\x64\x8B\x69\x20\x8B\x79\x24\x03\xEB\x83\x64\x24\x5C\x00\x03\xFB\x85\xC0\x74\x51\x8B\x75\x00\x03\xF3\x33\xD2\x0F\xBE\x06\xC1\xCA\x0D\x03\xD0\x46\x80\x7E\xFF\x00\x75\xF1\x39\x54\x24\x60\x74\x16\x8B\x44\x24\x5C\x83\xC5\x04\x40\x83\xC7\x02\x89\x44\x24\x5C\x3B\x41\x18\x72\xD0\xEB\x1F\x0F\xB7\x17\x83\xFA\xFF\x74\x17\x8B\x41\x1C\xFF\x74\x24\x68\xFF\x74\x24\x68\x8D\x04\x90\x8B\x04\x18\x03\xC3\xFF\xD0\x59\x59\xF6\x44\x24\x6C\x02\x74\x17\x8B\x6C\x24\x1C\x33\xC0\x68\x00\x80\x00\x00\x6A\x00\x55\xFF\xD0\x85\xC0\x75\x03\x55\xFF\xD0\x8B\xC3\x5F\x5E\x5D\x5B\x83\xC4\x48\xC3\x83\xEC\x10\x64\xA1\x30\x00\x00\x00\x53\x55\x56\x8B\x40\x0C\x57\x89\x4C\x24\x18\x8B\x70\x0C\xE9\x8A\x00\x00\x00\x8B\x46\x30\x33\xC9\x8B\x5E\x2C\x8B\x36\x89\x44\x24\x14\x8B\x42\x3C\x8B\x6C\x10\x78\x89\x6C\x24\x10\x85\xED\x74\x6D\xC1\xEB\x10\x33\xFF\x85\xDB\x74\x1F\x8B\x6C\x24\x14\x8A\x04\x2F\xC1\xC9\x0D\x3C\x61\x0F\xBE\xC0\x7C\x03\x83\xC1\xE0\x03\xC8\x47\x3B\xFB\x72\xE9\x8B\x6C\x24\x10\x8B\x44\x2A\x20\x33\xDB\x8B\x7C\x2A\x18\x03\xC2\x89\x7C\x24\x14\x85\xFF\x74\x31\x8B\x28\x33\xFF\x03\xEA\x83\xC0\x04\x89\x44\x24\x1C\x0F\xBE\x45\x00\xC1\xCF\x0D\x03\xF8\x45\x80\x7D\xFF\x00\x75\xF0\x8D\x04\x0F\x3B\x44\x24\x18\x74\x20\x8B\x44\x24\x1C\x43\x3B\x5C\x24\x14\x72\xCF\x8B\x56\x18\x85\xD2\x0F\x85\x6B\xFF\xFF\xFF\x33\xC0\x5F\x5E\x5D\x5B\x83\xC4\x10\xC3\x8B\x74\x24\x10\x8B\x44\x16\x24\x8D\x04\x58\x0F\xB7\x0C\x10\x8B\x44\x16\x1C\x8D\x04\x88\x8B\x04\x10\x03\xC2\xEB\xDB";
	LPSTR rdiShellcode64 = "\x48\x8B\xC4\x44\x89\x48\x20\x4C\x89\x40\x18\x89\x50\x10\x53\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x83\xEC\x78\x83\x60\x08\x00\x4C\x8B\xF1\xB9\x4C\x77\x26\x07\x33\xDB\xE8\xFB\x04\x00\x00\xB9\x49\xF7\x02\x78\x4C\x8B\xE8\xE8\xEE\x04\x00\x00\xB9\x58\xA4\x53\xE5\x48\x89\x44\x24\x20\xE8\xDF\x04\x00\x00\xB9\x10\xE1\x8A\xC3\x48\x8B\xE8\xE8\xD2\x04\x00\x00\xB9\xAF\xB1\x5C\x94\x48\x89\x44\x24\x30\xE8\xC3\x04\x00\x00\xB9\x33\x00\x9E\x95\x48\x89\x44\x24\x28\x4C\x8B\xE0\xE8\xB1\x04\x00\x00\x49\x63\x7E\x3C\x4C\x8B\xC8\x49\x03\xFE\x81\x3F\x50\x45\x00\x00\x74\x07\x33\xC0\xE9\x86\x04\x00\x00\xB8\x64\x86\x00\x00\x66\x39\x47\x04\x75\xEE\x41\xBF\x01\x00\x00\x00\x44\x84\x7F\x38\x75\xE2\x0F\xB7\x47\x06\x0F\xB7\x4F\x14\x44\x8B\x47\x38\x85\xC0\x74\x2B\x48\x83\xC1\x24\x8B\xD0\x48\x03\xCF\x83\x79\x04\x00\x75\x07\x8B\x01\x49\x03\xC0\xEB\x05\x8B\x01\x03\x41\x04\x48\x3B\xC3\x48\x0F\x47\xD8\x48\x83\xC1\x28\x49\x2B\xD7\x75\xDE\x48\x8D\x4C\x24\x38\x41\xFF\xD1\x44\x8B\x44\x24\x3C\x44\x8B\x4F\x50\x41\x8D\x40\xFF\xF7\xD0\x41\x8D\x50\xFF\x41\x03\xD1\x49\x8D\x48\xFF\x48\x23\xD0\x48\x03\xCB\x49\x8D\x40\xFF\x48\xF7\xD0\x48\x23\xC8\x48\x3B\xD1\x0F\x85\x6C\xFF\xFF\xFF\x48\x8B\x4F\x30\x41\x8B\xD1\xBB\x00\x30\x00\x00\x41\xB9\x04\x00\x00\x00\x44\x8B\xC3\xFF\xD5\x45\x33\xDB\x48\x8B\xF0\x48\x85\xC0\x75\x14\x8B\x57\x50\x44\x8D\x48\x04\x44\x8B\xC3\x33\xC9\xFF\xD5\x48\x8B\xF0\x45\x33\xDB\x44\x8B\x47\x54\x4D\x8B\xCE\x48\x8B\xCE\x49\x8B\xD3\xBD\x02\x00\x00\x00\x4D\x85\xC0\x74\x3F\x44\x8B\x94\x24\xE0\x00\x00\x00\x45\x23\xD7\x4D\x2B\xC7\x45\x85\xD2\x74\x19\x48\x8B\xC7\x49\x2B\xC6\x48\x3B\xD0\x73\x0E\x48\x8D\x42\xC4\x48\x3B\xC5\x76\x05\x44\x88\x19\xEB\x05\x41\x8A\x01\x88\x01\x49\x03\xD7\x4D\x03\xCF\x49\x03\xCF\x4D\x85\xC0\x75\xCC\x44\x0F\xB7\x57\x06\x0F\xB7\x47\x14\x4D\x85\xD2\x74\x38\x48\x8D\x4F\x2C\x48\x03\xC8\x8B\x51\xF8\x4D\x2B\xD7\x44\x8B\x01\x48\x03\xD6\x44\x8B\x49\xFC\x4D\x03\xC6\x4D\x85\xC9\x74\x10\x41\x8A\x00\x4D\x03\xC7\x88\x02\x49\x03\xD7\x4D\x2B\xCF\x75\xF0\x48\x83\xC1\x28\x4D\x85\xD2\x75\xCF\x8B\x9F\x90\x00\x00\x00\x48\x03\xDE\x8B\x43\x0C\x85\xC0\x0F\x84\x85\x00\x00\x00\x48\x8B\x6C\x24\x20\x8B\xC8\x48\x03\xCE\x41\xFF\xD5\x44\x8B\x3B\x4C\x8B\xE0\x44\x8B\x73\x10\x4C\x03\xFE\x4C\x03\xF6\x45\x33\xDB\xEB\x4B\x4D\x39\x1F\x7D\x29\x49\x63\x44\x24\x3C\x41\x0F\xB7\x17\x42\x8B\x8C\x20\x88\x00\x00\x00\x42\x8B\x44\x21\x10\x42\x8B\x4C\x21\x1C\x48\x2B\xD0\x49\x03\xCC\x8B\x04\x91\x49\x03\xC4\xEB\x12\x49\x8B\x16\x49\x8B\xCC\x48\x83\xC2\x02\x48\x03\xD6\xFF\xD5\x45\x33\xDB\x49\x89\x06\x49\x83\xC6\x08\x49\x83\xC7\x08\x4D\x39\x1E\x75\xB0\x8B\x43\x20\x48\x83\xC3\x14\x85\xC0\x75\x88\x4C\x8B\x64\x24\x28\x8D\x68\x02\x4C\x8B\xFE\x41\xBD\x01\x00\x00\x00\x4C\x2B\x7F\x30\x0F\x84\xA4\x00\x00\x00\x44\x39\x9F\xB4\x00\x00\x00\x0F\x84\x97\x00\x00\x00\x44\x8B\x87\xB0\x00\x00\x00\x4C\x03\xC6\x41\x8B\x40\x04\x85\xC0\x0F\x84\x81\x00\x00\x00\xBB\xFF\x0F\x00\x00\x41\x8B\x10\x4D\x8D\x50\x08\x44\x8B\xC8\x48\x03\xD6\x49\x83\xE9\x08\x49\xD1\xE9\x74\x57\x41\x0F\xB7\x0A\x4D\x2B\xCD\x0F\xB7\xC1\x66\xC1\xE8\x0C\x66\x83\xF8\x0A\x75\x09\x48\x23\xCB\x4C\x01\x3C\x11\xEB\x32\x66\x83\xF8\x03\x75\x09\x48\x23\xCB\x44\x01\x3C\x11\xEB\x23\x66\x41\x3B\xC5\x75\x10\x48\x23\xCB\x49\x8B\xC7\x48\xC1\xE8\x10\x66\x01\x04\x11\xEB\x0D\x66\x3B\xC5\x75\x08\x48\x23\xCB\x66\x44\x01\x3C\x11\x4C\x03\xD5\x4D\x85\xC9\x75\xA9\x41\x8B\x40\x04\x4C\x03\xC0\x41\x8B\x40\x04\x85\xC0\x75\x84\x0F\xB7\x6F\x06\x0F\xB7\x47\x14\x48\x85\xED\x0F\x84\xD9\x00\x00\x00\x8B\x9C\x24\xC0\x00\x00\x00\x4C\x8D\x77\x3C\x4C\x8B\x6C\x24\x30\x4C\x03\xF0\x41\xB9\x01\x00\x00\x00\x49\x2B\xE9\x45\x39\x5E\xEC\x0F\x86\xA2\x00\x00\x00\x45\x8B\x06\x41\x8B\xD0\xC1\xEA\x1E\x41\x8B\xC0\x41\x8B\xC8\xC1\xE8\x1D\x41\x23\xD1\xC1\xE9\x1F\x41\x23\xC1\x75\x1C\x85\xD2\x75\x0C\xF7\xD9\x1B\xDB\x83\xE3\x07\x41\x03\xD9\xEB\x3B\xF7\xD9\x1B\xDB\x83\xE3\x02\x83\xC3\x02\xEB\x2F\x85\xD2\x75\x18\x85\xC9\x75\x05\x8D\x5A\x10\xEB\x22\x85\xD2\x75\x0B\x85\xC9\x74\x1A\xBB\x80\x00\x00\x00\xEB\x13\x85\xC9\x75\x05\x8D\x59\x20\xEB\x0A\x85\xC9\xB8\x40\x00\x00\x00\x0F\x45\xD8\x41\x8B\x4E\xE8\x4C\x8D\x8C\x24\xC0\x00\x00\x00\x41\x8B\x56\xEC\x8B\xC3\x0F\xBA\xE8\x09\x41\x81\xE0\x00\x00\x00\x04\x0F\x44\xC3\x48\x03\xCE\x44\x8B\xC0\x8B\xD8\x41\xFF\xD5\x45\x33\xDB\x85\xC0\x0F\x84\x75\xFC\xFF\xFF\x45\x8D\x4B\x01\x49\x83\xC6\x28\x48\x85\xED\x0F\x85\x44\xFF\xFF\xFF\x44\x8D\x6D\x01\x8B\x5F\x28\x45\x33\xC0\x33\xD2\x48\x83\xC9\xFF\x48\x03\xDE\x41\xFF\xD4\x4D\x8B\xC5\x41\x8B\xD5\x48\x8B\xCE\xFF\xD3\x8B\xAC\x24\xC8\x00\x00\x00\x45\x33\xF6\x85\xED\x0F\x84\x99\x00\x00\x00\x44\x39\xB7\x8C\x00\x00\x00\x0F\x84\x8C\x00\x00\x00\x8B\x97\x88\x00\x00\x00\x48\x03\xD6\x44\x8B\x5A\x18\x45\x85\xDB\x74\x7A\x44\x39\x72\x14\x74\x74\x44\x8B\x52\x20\x41\x8B\xDE\x44\x8B\x4A\x24\x4C\x03\xD6\x4C\x03\xCE\x45\x85\xDB\x74\x5E\x45\x8B\x02\x41\x8B\xCE\x4C\x03\xC6\x41\x0F\xBE\x00\x4D\x03\xC5\xC1\xC9\x0D\x03\xC8\x45\x38\x70\xFF\x75\xEE\x3B\xE9\x74\x12\x41\x03\xDD\x49\x83\xC2\x04\x49\x83\xC1\x02\x41\x3B\xDB\x72\xD1\xEB\x2D\x41\x0F\xB7\x01\x83\xF8\xFF\x74\x24\x8B\x52\x1C\x48\x8B\x8C\x24\xD0\x00\x00\x00\xC1\xE0\x02\x48\x98\x48\x03\xC6\x44\x8B\x04\x02\x8B\x94\x24\xD8\x00\x00\x00\x4C\x03\xC6\x41\xFF\xD0\xF6\x84\x24\xE0\x00\x00\x00\x02\x74\x18\x33\xD2\x41\xB8\x00\x80\x00\x00\x49\x8B\xCF\x41\xFF\xD6\x85\xC0\x75\x06\x49\x8B\xCF\x41\xFF\xD6\x48\x8B\xC6\x48\x83\xC4\x78\x41\x5F\x41\x5E\x41\x5D\x41\x5C\x5F\x5E\x5D\x5B\xC3\xCC\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x10\x65\x48\x8B\x04\x25\x60\x00\x00\x00\x8B\xF1\x48\x8B\x50\x18\x4C\x8B\x4A\x10\x4D\x8B\x41\x30\x4D\x85\xC0\x0F\x84\xB4\x00\x00\x00\x41\x0F\x10\x41\x58\x49\x63\x40\x3C\x33\xD2\x4D\x8B\x09\xF3\x0F\x7F\x04\x24\x42\x8B\x9C\x00\x88\x00\x00\x00\x85\xDB\x74\xD4\x48\x8B\x04\x24\x48\xC1\xE8\x10\x44\x0F\xB7\xD0\x45\x85\xD2\x74\x21\x48\x8B\x4C\x24\x08\x45\x8B\xDA\x0F\xBE\x01\xC1\xCA\x0D\x80\x39\x61\x7C\x03\x83\xC2\xE0\x03\xD0\x48\xFF\xC1\x49\x83\xEB\x01\x75\xE7\x4D\x8D\x14\x18\x33\xC9\x41\x8B\x7A\x20\x49\x03\xF8\x41\x39\x4A\x18\x76\x8F\x8B\x1F\x45\x33\xDB\x49\x03\xD8\x48\x8D\x7F\x04\x0F\xBE\x03\x48\xFF\xC3\x41\xC1\xCB\x0D\x44\x03\xD8\x80\x7B\xFF\x00\x75\xED\x41\x8D\x04\x13\x3B\xC6\x74\x0D\xFF\xC1\x41\x3B\x4A\x18\x72\xD1\xE9\x5B\xFF\xFF\xFF\x41\x8B\x42\x24\x03\xC9\x49\x03\xC0\x0F\xB7\x14\x01\x41\x8B\x4A\x1C\x49\x03\xC8\x8B\x04\x91\x49\x03\xC0\xEB\x02\x33\xC0\x48\x8B\x5C\x24\x20\x48\x8B\x74\x24\x28\x48\x83\xC4\x10\x5F\xC3";
	DWORD rdiShellcode32Length = 1356, rdiShellcode64Length = 1569;
#endif

	if (Is64BitDLL((UINT_PTR)inBytes))
	{

		rdiShellcode = rdiShellcode64;
		rdiShellcodeLength = rdiShellcode64Length;

		if (rdiShellcode == NULL || rdiShellcodeLength == 0) return 0;

		BYTE bootstrap[64] = { 0 };
		DWORD i = 0;

		// call next instruction (Pushes next instruction address to stack)
		bootstrap[i++] = 0xe8;
		bootstrap[i++] = 0x00;
		bootstrap[i++] = 0x00;
		bootstrap[i++] = 0x00;
		bootstrap[i++] = 0x00;

		// Set the offset to our DLL from pop result
		dllOffset = sizeof(bootstrap) - i + rdiShellcodeLength;

		// pop rcx - Capture our current location in memory
		bootstrap[i++] = 0x59;

		// mov r8, rcx - copy our location in memory to r8 before we start modifying RCX
		bootstrap[i++] = 0x49;
		bootstrap[i++] = 0x89;
		bootstrap[i++] = 0xc8;

		// add rcx, <Offset of the DLL>
		bootstrap[i++] = 0x48;
		bootstrap[i++] = 0x81;
		bootstrap[i++] = 0xc1;
		MoveMemory(bootstrap + i, &dllOffset, sizeof(dllOffset));
		i += sizeof(dllOffset);

		// mov edx, <hash of function>
		bootstrap[i++] = 0xba;
		MoveMemory(bootstrap + i, &userFunction, sizeof(userFunction));
		i += sizeof(userFunction);

		// Setup the location of our user data
		// add r8, <Offset of the DLL> + <Length of DLL>
		bootstrap[i++] = 0x49;
		bootstrap[i++] = 0x81;
		bootstrap[i++] = 0xc0;
		userDataLocation = dllOffset + length;
		MoveMemory(bootstrap + i, &userDataLocation, sizeof(userDataLocation));
		i += sizeof(userDataLocation);

		// mov r9d, <Length of User Data>
		bootstrap[i++] = 0x41;
		bootstrap[i++] = 0xb9;
		MoveMemory(bootstrap + i, &userLength, sizeof(userLength));
		i += sizeof(userLength);

		// push rsi - save original value
		bootstrap[i++] = 0x56;

		// mov rsi, rsp - store our current stack pointer for later
		bootstrap[i++] = 0x48;
		bootstrap[i++] = 0x89;
		bootstrap[i++] = 0xe6;

		// and rsp, 0x0FFFFFFFFFFFFFFF0 - Align the stack to 16 bytes
		bootstrap[i++] = 0x48;
		bootstrap[i++] = 0x83;
		bootstrap[i++] = 0xe4;
		bootstrap[i++] = 0xf0;

		// sub rsp, 0x30 - Create some breathing room on the stack 
		bootstrap[i++] = 0x48;
		bootstrap[i++] = 0x83;
		bootstrap[i++] = 0xec;
		bootstrap[i++] = 6 * 8; // 32 bytes for shadow space + 8 bytes for last arg + 8 bytes for stack alignment

		// mov dword ptr [rsp + 0x20], <Flags> - Push arg 5 just above shadow space
		bootstrap[i++] = 0xC7;
		bootstrap[i++] = 0x44;
		bootstrap[i++] = 0x24;
		bootstrap[i++] = 4 * 8;
		MoveMemory(bootstrap + i, &flags, sizeof(flags));
		i += sizeof(flags);

		// call - Transfer execution to the RDI
		bootstrap[i++] = 0xe8;
		bootstrap[i++] = sizeof(bootstrap) - i - 4; // Skip over the remainder of instructions
		bootstrap[i++] = 0x00;
		bootstrap[i++] = 0x00;
		bootstrap[i++] = 0x00;

		// mov rsp, rsi - Reset our original stack pointer
		bootstrap[i++] = 0x48;
		bootstrap[i++] = 0x89;
		bootstrap[i++] = 0xf4;

		// pop rsi - Put things back where we left them
		bootstrap[i++] = 0x5e;

		// ret - return to caller
		bootstrap[i++] = 0xc3;

		// Ends up looking like this in memory:
		// Bootstrap shellcode
		// RDI shellcode
		// DLL bytes
		// User data
		outLength = length + userLength + rdiShellcodeLength + sizeof(bootstrap);
		outBytes = (LPSTR)malloc(outLength);
		MoveMemory(outBytes, bootstrap, sizeof(bootstrap));
		MoveMemory(outBytes + sizeof(bootstrap), rdiShellcode, rdiShellcodeLength);
		MoveMemory(outBytes + sizeof(bootstrap) + rdiShellcodeLength, inBytes, length);
		MoveMemory(outBytes + sizeof(bootstrap) + rdiShellcodeLength + length, userData, userLength);

	}
	else { // 32 bit

		rdiShellcode = rdiShellcode32;
		rdiShellcodeLength = rdiShellcode32Length;

		if (rdiShellcode == NULL || rdiShellcodeLength == 0) return 0;

		BYTE bootstrap[46] = { 0 };
		DWORD i = 0;

		// call next instruction (Pushes next instruction address to stack)
		bootstrap[i++] = 0xe8;
		bootstrap[i++] = 0x00;
		bootstrap[i++] = 0x00;
		bootstrap[i++] = 0x00;
		bootstrap[i++] = 0x00;

		// Set the offset to our DLL from pop result
		dllOffset = sizeof(bootstrap) - i + rdiShellcodeLength;

		// pop eax - Capture our current location in memory
		bootstrap[i++] = 0x58;

		// push ebp
		bootstrap[i++] = 0x55;

		// move ebp, esp
		bootstrap[i++] = 0x89;
		bootstrap[i++] = 0xe5;

		// mov ebx, eax - copy our location in memory to ebx before we start modifying eax
		bootstrap[i++] = 0x89;
		bootstrap[i++] = 0xc3;

		// add eax, <Offset to the DLL>
		bootstrap[i++] = 0x05;
		MoveMemory(bootstrap + i, &dllOffset, sizeof(dllOffset));
		i += sizeof(dllOffset);

		// add ebx, <Offset to the DLL> + <Size of DLL>
		bootstrap[i++] = 0x81;
		bootstrap[i++] = 0xc3;
		userDataLocation = dllOffset + length;
		MoveMemory(bootstrap + i, &userDataLocation, sizeof(userDataLocation));
		i += sizeof(userDataLocation);

		// push <Flags>
		bootstrap[i++] = 0x68;
		MoveMemory(bootstrap + i, &flags, sizeof(flags));
		i += sizeof(flags);

		// push <Length of User Data>
		bootstrap[i++] = 0x68;
		MoveMemory(bootstrap + i, &userLength, sizeof(userLength));
		i += sizeof(userLength);

		// push ebx
		bootstrap[i++] = 0x53;

		// push <hash of function>
		bootstrap[i++] = 0x68;
		MoveMemory(bootstrap + i, &userFunction, sizeof(userFunction));
		i += sizeof(userFunction);

		// push eax
		bootstrap[i++] = 0x50;

		// call - Transfer execution to the RDI
		bootstrap[i++] = 0xe8;
		bootstrap[i++] = sizeof(bootstrap) - i - 4; // Skip the remainder of instructions
		bootstrap[i++] = 0x00;
		bootstrap[i++] = 0x00;
		bootstrap[i++] = 0x00;

		// add esp, 0x14 - correct the stack pointer
		//bootstrap[i++] = 0x83;
		//bootstrap[i++] = 0xc4;
		//bootstrap[i++] = 0x14;

		// leave
		bootstrap[i++] = 0xc9;

		// ret - return to caller
		bootstrap[i++] = 0xc3;

		// Ends up looking like this in memory:
		// Bootstrap shellcode
		// RDI shellcode
		// DLL bytes
		// User data
		outLength = length + userLength + rdiShellcodeLength + sizeof(bootstrap);
		outBytes = (LPSTR)malloc(outLength);
		MoveMemory(outBytes, bootstrap, sizeof(bootstrap));
		MoveMemory(outBytes + sizeof(bootstrap), rdiShellcode, rdiShellcodeLength);
		MoveMemory(outBytes + sizeof(bootstrap) + rdiShellcodeLength, inBytes, length);
		MoveMemory(outBytes + sizeof(bootstrap) + rdiShellcodeLength + length, userData, userLength);
	}

	return true;
}

typedef UINT_PTR(WINAPI * RDI)();
typedef void(WINAPI * Function)();
typedef BOOL(__cdecl * EXPORTEDFUNCTION)(LPVOID, DWORD);

//DWORD demoSRDI(PCWSTR pszLibFile, DWORD dwProcessId)
DWORD demoSRDI(PCWSTR pszLibFile)
{
	LPSTR finalShellcode = NULL, data = NULL;
	DWORD finalSize, dataSize;
	DWORD dwOldProtect1 = 0;
	SYSTEM_INFO sysInfo;

	// For any MessageBox testing in the blob
	HMODULE test = LoadLibraryA("User32.dll");

	
	char* input = WideStringToCharString(pszLibFile);

	if (!GetFileContents(input, &data, dataSize)) {
		printf("\n[!] Failed to load file\n");
		return 0;
	}

	if (data[0] == 'M' && data[1] == 'Z') {
		printf("[+] File is a DLL, attempting to convert\n");

		if (!ConvertToShellcode(data, dataSize, HashFunctionName("SayHello"), "dave", 5, SRDI_CLEARHEADER, finalShellcode, finalSize)) {
			printf("[!] Failed to convert DLL\n");
			return 0;
		}

		printf("[+] Successfully Converted\n");
	}
	else {
		finalShellcode = data;
		finalSize = dataSize;
	}

	GetNativeSystemInfo(&sysInfo);

	// Only set the first page to RWX
	// This is should sufficiently cover the sRDI shellcode up top
	if (VirtualProtect(finalShellcode, sysInfo.dwPageSize, PAGE_EXECUTE_READWRITE, &dwOldProtect1)) {
		RDI rdi = (RDI)(finalShellcode);

		printf("[+] Executing RDI\n");
		UINT_PTR hLoadedDLL = rdi(); // Excute DLL

		free(finalShellcode); // Free the RDI blob. We no longer need it.

		Function exportedFunction = (Function)sRDIGetProcAddressR(hLoadedDLL, "SayGoodbye");
		if (exportedFunction) {
			printf("[+] Calling exported functon\n");
			exportedFunction();
		}
	}

	return DWORD(0);
}