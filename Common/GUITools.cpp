//---------------------------------------------------------------------------
#define NO_WIN32_LEAN_AND_MEAN

#include <shlobj.h>
#include <Common.h>

#include "GUITools.h"
#include "GUIConfiguration.h"
#include <TextsCore.h>
#include <CoreMain.h>
#include <SessionData.h>
//---------------------------------------------------------------------------
bool FindFile(std::wstring & Path)
{
  bool Result = FileExists(Path);
  if (!Result)
  {
    int Len = GetEnvironmentVariable("PATH", NULL, 0);
    if (Len > 0)
    {
      std::wstring Paths;
      Paths.resize(Len - 1);
      GetEnvironmentVariable("PATH", Paths.c_str(), Len);

      std::wstring NewPath = FileSearch(ExtractFileName(Path), Paths);
      Result = !NewPath.IsEmpty();
      if (Result)
      {
        Path = NewPath;
      }
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
bool FileExistsEx(std::wstring Path)
{
  return FindFile(Path);
}
//---------------------------------------------------------------------------
void OpenSessionInPutty(const std::wstring PuttyPath,
  TSessionData * SessionData, std::wstring Password)
{
  std::wstring Program, Params, Dir;
  SplitCommand(PuttyPath, Program, Params, Dir);
  Program = ExpandEnvironmentVariables(Program);
  if (FindFile(Program))
  {
    std::wstring SessionName;
    TRegistryStorage * Storage = NULL;
    TSessionData * ExportData = NULL;
    TRegistryStorage * SourceStorage = NULL;
    try
    {
      Storage = new TRegistryStorage(Configuration->PuttySessionsKey);
      Storage->AccessMode = smReadWrite;
      // make it compatible with putty
      Storage->MungeStringValues = false;
      if (Storage->OpenRootKey(true))
      {
        if (Storage->KeyExists(GetSessionData()->StorageKey))
        {
          SessionName = GetSessionData()->SessionName;
        }
        else
        {
          SourceStorage = new TRegistryStorage(Configuration->PuttySessionsKey);
          SourceStorage->MungeStringValues = false;
          if (SourceStorage->OpenSubKey(StoredSessions->DefaultSettings->Name, false) &&
              Storage->OpenSubKey(GUIConfiguration->PuttySession, true))
          {
            Storage->Copy(SourceStorage);
            Storage->CloseSubKey();
          }

          ExportData = new TSessionData("");
          ExportData->Assign(GetSessionData());
          ExportData->Modified = true;
          ExportData->Name = GUIConfiguration->PuttySession;
          ExportData->Password = "";

          if (GetSessionData()->FSProtocol == fsFTP)
          {
            if (GUIConfiguration->TelnetForFtpInPutty)
            {
              ExportData->Protocol = ptTelnet;
              ExportData->PortNumber = 23;
              // PuTTY  does not allow -pw for telnet
              Password = "";
            }
            else
            {
              ExportData->Protocol = ptSSH;
              ExportData->PortNumber = 22;
            }
          }

          ExportData->Save(Storage, true);
          SessionName = GUIConfiguration->PuttySession;
        }
      }
    }
    __finally
    {
      delete Storage;
      delete ExportData;
      delete SourceStorage;
    }

    if (!Params.IsEmpty())
    {
      Params += " ";
    }
    if (!Password.IsEmpty())
    {
      Params += ::FORMAT(L"-pw %s ", (EscapePuttyCommandParam(Password)));
    }
    Params += ::FORMAT(L"-load %s", (EscapePuttyCommandParam(SessionName)));

    if (!ExecuteShell(Program, Params))
    {
      throw std::exception(FMTLOAD(EXECUTE_APP_ERROR, (Program)));
    }
  }
  else
  {
    throw std::exception(FMTLOAD(FILE_NOT_FOUND, (Program)));
  }
}
//---------------------------------------------------------------------------
bool ExecuteShell(const std::wstring Path, const std::wstring Params)
{
  return ((int)ShellExecute(NULL, "open", (char*)Path.data(),
    (char*)Params.data(), NULL, SW_SHOWNORMAL) > 32);
}
//---------------------------------------------------------------------------
bool ExecuteShell(const std::wstring Path, const std::wstring Params,
  HANDLE & Handle)
{
  bool Result;

  TShellExecuteInfo ExecuteInfo;
  memset(&ExecuteInfo, 0, sizeof(ExecuteInfo));
  ExecuteInfo.cbSize = sizeof(ExecuteInfo);
  ExecuteInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
  ExecuteInfo.hwnd = Application->Handle;
  ExecuteInfo.lpFile = (char*)Path.data();
  ExecuteInfo.lpParameters = (char*)Params.data();
  ExecuteInfo.nShow = SW_SHOW;

  Result = (ShellExecuteEx(&ExecuteInfo) != 0);
  if (Result)
  {
    Handle = ExecuteInfo.hProcess;
  }
  return Result;
}
//---------------------------------------------------------------------------
bool ExecuteShellAndWait(HWND Handle, const std::wstring Path,
  const std::wstring Params, TProcessMessagesEvent ProcessMessages)
{
  bool Result;

  TShellExecuteInfo ExecuteInfo;
  memset(&ExecuteInfo, 0, sizeof(ExecuteInfo));
  ExecuteInfo.cbSize = sizeof(ExecuteInfo);
  ExecuteInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
  ExecuteInfo.hwnd = Handle;
  ExecuteInfo.lpFile = (char*)Path.data();
  ExecuteInfo.lpParameters = (char*)Params.data();
  ExecuteInfo.nShow = SW_SHOW;

  Result = (ShellExecuteEx(&ExecuteInfo) != 0);
  if (Result)
  {
    if (ProcessMessages != NULL)
    {
      unsigned long WaitResult;
      do
      {
        WaitResult = WaitForSingleObject(ExecuteInfo.hProcess, 200);
        if (WaitResult == WAIT_FAILED)
        {
          throw std::exception(LoadStr(DOCUMENT_WAIT_ERROR));
        }
        ProcessMessages();
      }
      while (WaitResult == WAIT_TIMEOUT);
    }
    else
    {
      WaitForSingleObject(ExecuteInfo.hProcess, INFINITE);
    }
  }
  return Result;
}
//---------------------------------------------------------------------------
bool ExecuteShellAndWait(HWND Handle, const std::wstring Command,
  TProcessMessagesEvent ProcessMessages)
{
  std::wstring Program, Params, Dir;
  SplitCommand(Command, Program, Params, Dir);
  return ExecuteShellAndWait(Handle, Program, Params, ProcessMessages);
}
//---------------------------------------------------------------------------
bool SpecialFolderLocation(int PathID, std::wstring & Path)
{
  LPITEMIDLIST Pidl;
  char Buf[256];
  if (SHGetSpecialFolderLocation(NULL, PathID, &Pidl) == NO_ERROR &&
      SHGetPathFromIDList(Pidl, Buf))
  {
    Path = std::wstring(Buf);
    return true;
  }
  return false;
}
//---------------------------------------------------------------------------
std::wstring ItemsFormatString(const std::wstring SingleItemFormat,
  const std::wstring MultiItemsFormat, int Count, const std::wstring FirstItem)
{
  std::wstring Result;
  if (Count == 1)
  {
    Result = FORMAT(SingleItemFormat, (FirstItem));
  }
  else
  {
    Result = FORMAT(MultiItemsFormat, (Count));
  }
  return Result;
}
//---------------------------------------------------------------------------
std::wstring ItemsFormatString(const std::wstring SingleItemFormat,
  const std::wstring MultiItemsFormat, TStrings * Items)
{
  return ItemsFormatString(SingleItemFormat, MultiItemsFormat,
    Items->GetCount(), (Items->GetCount() > 0 ? Items->GetString(0] : std::wstring()));
}
//---------------------------------------------------------------------------
std::wstring FileNameFormatString(const std::wstring SingleFileFormat,
  const std::wstring MultiFilesFormat, TStrings * Files, bool Remote)
{
  assert(Files != NULL);
  std::wstring Item;
  if (Files->GetCount() > 0)
  {
    Item = Remote ? UnixExtractFileName(Files->GetString(0]) :
      ExtractFileName(Files->GetString(0]);
  }
  return ItemsFormatString(SingleFileFormat, MultiFilesFormat,
    Files->GetCount(), Item);
}
//---------------------------------------------------------------------
std::wstring FormatBytes(__int64 Bytes, bool UseOrders)
{
  std::wstring Result;

  if (!UseOrders || (Bytes < __int64(100*1024)))
  {
    Result = FormatFloat("#,##0 \"B\"", Bytes);
  }
  else if (Bytes < __int64(100*1024*1024))
  {
    Result = FormatFloat("#,##0 \"KiB\"", Bytes / 1024);
  }
  else
  {
    Result = FormatFloat("#,##0 \"MiB\"", Bytes / (1024*1024));
  }
  return Result;
}
//---------------------------------------------------------------------------
std::wstring UniqTempDir(const std::wstring BaseDir, const std::wstring Identity,
  bool Mask)
{
  std::wstring TempDir;
  do
  {
    TempDir = BaseDir.IsEmpty() ? SystemTemporaryDirectory() : BaseDir;
    TempDir = IncludeTrailingBackslash(TempDir) + Identity;
    if (Mask)
    {
      TempDir += "?????";
    }
    else
    {
      TempDir += IncludeTrailingBackslash(FormatDateTime("nnzzz", Now()));
    };
  }
  while (!Mask && DirectoryExists(TempDir));

  return TempDir;
}
//---------------------------------------------------------------------------
bool DeleteDirectory(const std::wstring DirName)
{
  TSearchRec sr;
  bool retval = true;
  if (FindFirst(DirName + "\\*", faAnyFile, sr) == 0) // VCL Function
  {
    if (FLAGSET(sr.Attr, faDirectory))
    {
      if (sr.Name != "." && sr.Name != "..")
        retval = DeleteDirectory(DirName + "\\" + sr.Name);
    }
    else
    {
      retval = DeleteFile(DirName + "\\" + sr.Name);
    }

    if (retval)
    {
      while (FindNext(sr) == 0)
      { // VCL Function
        if (FLAGSET(sr.Attr, faDirectory))
        {
          if (sr.Name != "." && sr.Name != "..")
            retval = DeleteDirectory(DirName + "\\" + sr.Name);
        }
        else
        {
          retval = DeleteFile(DirName + "\\" + sr.Name);
        }

        if (!retval) break;
      }
    }
  }
  FindClose(sr);
  if (retval) retval = RemoveDir(DirName); // VCL function
  return retval;
}
//---------------------------------------------------------------------------
std::wstring FormatDateTimeSpan(const std::wstring TimeFormat, TDateTime DateTime)
{
  std::wstring Result;
  if (int(DateTime) > 0)
  {
    Result = IntToStr(int(DateTime)) + ", ";
  }
  // days are decremented, because when there are to many of them,
  // "integer overflow" error occurs
  Result += FormatDateTime(TimeFormat, DateTime - int(DateTime));
  return Result;
}
//---------------------------------------------------------------------------
TLocalCustomCommand::TLocalCustomCommand()
{
}
//---------------------------------------------------------------------------
TLocalCustomCommand::TLocalCustomCommand(const TCustomCommandData & Data,
    const std::wstring & Path) :
  TFileCustomCommand(Data, Path)
{
}
//---------------------------------------------------------------------------
TLocalCustomCommand::TLocalCustomCommand(const TCustomCommandData & Data,
  const std::wstring & Path, const std::wstring & FileName,
  const std::wstring & LocalFileName, const std::wstring & FileList) :
  TFileCustomCommand(Data, Path, FileName, FileList)
{
  FLocalFileName = LocalFileName;
}
//---------------------------------------------------------------------------
int TLocalCustomCommand::PatternLen(int Index, char PatternCmd)
{
  int Len;
  if (PatternCmd == '^')
  {
    Len = 3;
  }
  else
  {
    Len = TFileCustomCommand::PatternLen(Index, PatternCmd);
  }
  return Len;
}
//---------------------------------------------------------------------------
bool TLocalCustomCommand::PatternReplacement(int Index,
  const std::wstring & Pattern, std::wstring & Replacement, bool & Delimit)
{
  bool Result;
  if (Pattern == "!^!")
  {
    Replacement = FLocalFileName;
    Result = true;
  }
  else
  {
    Result = TFileCustomCommand::PatternReplacement(Index, Pattern, Replacement, Delimit);
  }
  return Result;
}
//---------------------------------------------------------------------------
void TLocalCustomCommand::DelimitReplacement(
  std::wstring & /*Replacement*/, char /*Quote*/)
{
  // never delimit local commands
}
//---------------------------------------------------------------------------
bool TLocalCustomCommand::HasLocalFileName(const std::wstring & Command)
{
  return FindPattern(Command, '^');
}
//---------------------------------------------------------------------------
bool TLocalCustomCommand::IsFileCommand(const std::wstring & Command)
{
  return TFileCustomCommand::IsFileCommand(Command) || HasLocalFileName(Command);
}
