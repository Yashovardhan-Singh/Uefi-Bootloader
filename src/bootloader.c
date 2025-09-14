#include <efi.h>

EFI_STATUS
EFIAPI
efi_main(
	IN EFI_HANDLE ImageHandle,
	IN EFI_SYSTEM_TABLE *SystemTable
) {
	SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
  
	SystemTable->ConOut->OutputString(
		SystemTable->ConOut, u"Hello, World!\r\n");
  
	SystemTable->ConOut->OutputString(
		SystemTable->ConOut, u"Press any key to shutdown...");

	EFI_INPUT_KEY key;
	while (SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &key)
		!= EFI_SUCCESS);

	SystemTable->RuntimeServices->ResetSystem(
		EfiResetShutdown, EFI_SUCCESS, 0,NULL);

  return EFI_SUCCESS;
}
