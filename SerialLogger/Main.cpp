#include<windows.h>
#include<stdio.h>
#include<iostream>
#include<string>
#include<sstream>
#include<fstream>
#include<svector/time.h>
#include<deque>
#include<thread>

const char lineEnd[2] = { 13,10 };

void throwWindowsError(std::string str)
{
    auto code = GetLastError();
    std::ostringstream WinError;
    WinError << "Windows error code: " << code;
    throw(str + " " + WinError.str());
}

void readData(HANDLE hComm, std::wstring filename, size_t linesPerTimeRecord)
{
    size_t linesSinceLastTimeRecord = 0;
    FILETIME timeRecord{ 0,0 };

    sci::UtcTime begin = sci::UtcTime::now();
    std::fstream fout;
    fout.open(filename, std::ios::out);
    if (!fout.is_open())
    {
        std::cout << "Failed to open data file." << std::endl;
        return;
    }
    fout << "PAMB0001";
    fout << lineEnd;

    unsigned char singleByte;
    bool getTimestampAtNextByte = true;
    while (1)
    {
        DWORD nRead;
        if (ReadFile(hComm, &singleByte, 1, &nRead, NULL) && nRead == 1)
        {
            if (getTimestampAtNextByte)
            {
                GetSystemTimePreciseAsFileTime(&timeRecord);
                std::cout << "tr " << timeRecord.dwHighDateTime << " " << timeRecord.dwLowDateTime << "\n";
                fout << "tr " << timeRecord.dwHighDateTime << " " << timeRecord.dwLowDateTime << "\n";
                fout.flush();//this seems like a good time to flush the file buffer
                linesSinceLastTimeRecord = 0;
            }

            //check if we got a line end
            if (lineEnd[1] == singleByte)
                ++linesSinceLastTimeRecord;
            if(linesSinceLastTimeRecord == linesPerTimeRecord)
                getTimestampAtNextByte = true;
            else
                getTimestampAtNextByte = false;

            //output the data
#ifdef _DEBUG
            std::cout.write((char*)&singleByte, 1);
#endif
            fout.write((char*)&singleByte, 1);
        }
    }
}

std::string parityDescription(BYTE parity)
{
    if (parity == PARITY_NONE)
        return "none";
    if (parity == PARITY_ODD)
        return "odd";
    if (parity == PARITY_EVEN)
        return "even";
    if (parity == PARITY_MARK)
        return "mark";
    if (parity == PARITY_SPACE)
        return "space";
    return "unknown";
}

int wmain( int argc, wchar_t *argv[])
{
    if (argc != 4)
    {
        std::cout << "please enter com port and filename and lines per time record as arguments, e.g.\nSerialLogger COM4 \"C:\\Data\\my file.txt\" 400";
        return 1;
    }
    std::wstring comport = L"\\\\.\\" + std::wstring(argv[1]);
    std::wstring filename = argv[2];

    wchar_t* end = nullptr;
    size_t linesPerTimeRecord = std::wcstoul(argv[3], &end, 10);
    if (end == argv[3])
    {
        std::wcout << "could not convert the third argument to an integer" << std::endl;
        return 1;
    }

    HANDLE hComm;

    hComm = CreateFileW(comport.c_str(),                //port name
        GENERIC_READ | GENERIC_WRITE, //Read/Write
        0,                            // No Sharing
        NULL,                         // No Security
        OPEN_EXISTING,// Open existing port only
        0,            // Non Overlapped I/O
        NULL);        // Null for Comm Devices

    if (hComm == INVALID_HANDLE_VALUE)
        std::cout << "Error in opening serial port";
    else
        std::cout << "Opening serial port successful";

    try
    {
        DCB commState;
        if (GetCommState(hComm, &commState) == 0)
            throwWindowsError(std::string("Error getting initial comm state"));
        std::cout << "Initial settings" << std::endl;
        std::cout << "baud  :" << commState.BaudRate << std::endl;
        std::cout << "stop  :" << (commState.StopBits == ONESTOPBIT ? 1 : 2) << std::endl;
        std::cout << "n     :" << int(commState.ByteSize) << std::endl;
        std::cout << "Parity:" << parityDescription(commState.Parity) << std::endl;
        std::cout << std::endl;

        commState.BaudRate = CBR_38400;
        commState.StopBits = ONESTOPBIT;
        commState.ByteSize = 8;
        commState.Parity = PARITY_NONE;

        if (SetCommState(hComm, &commState) == 0)
            throwWindowsError(std::string("Error setting com state."));

        if (GetCommState(hComm, &commState) == 0)
            throwWindowsError(std::string("Error getting comm state after changing settings"));
        std::cout << "New settings" << std::endl;
        std::cout << "baud  :" << commState.BaudRate << std::endl;
        std::cout << "stop  :" << (commState.StopBits == ONESTOPBIT ? 1 : 2) << std::endl;
        std::cout << "n     :" << int(commState.ByteSize) << std::endl;
        std::cout << "Parity:" << parityDescription(commState.Parity) << std::endl;
        std::cout << std::endl;

        COMMTIMEOUTS timeouts;

        if (GetCommTimeouts(hComm, &timeouts) == 0)
            throwWindowsError("Failed to get initial comm timeout values.");
        std::cout << "Initial timeouts" << std::endl;
        std::cout << "Timeout interval per individual byte (ms):" << timeouts.ReadIntervalTimeout << std::endl;
        std::cout << "Timeout interval per byte for a read command (ms):" << timeouts.ReadTotalTimeoutMultiplier << std::endl;
        std::cout << "Extra Timeout interval per read command (ms):" << timeouts.ReadTotalTimeoutConstant << std::endl;
        std::cout << "Timeout interval per byte for a wrte command(ms):" << timeouts.WriteTotalTimeoutMultiplier << std::endl;
        std::cout << "Extra Timeout interval per write command (ms)(ms):" << timeouts.WriteTotalTimeoutConstant << std::endl;
        std::cout << std::endl;

        //wait up to 100+10*nbytes milliseconds for each read/write operation with a maximum of 100 milliseconds between each read character.
        //set ReadIntervalTimeout to MAXDWORD and the other tow read values to zero to return immediately
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 0;

        if (SetCommTimeouts(hComm, &timeouts) == 0)
            throwWindowsError("Failed to set comm timeout values.");

        if (GetCommTimeouts(hComm, &timeouts) == 0)
            throwWindowsError("Failed to get new comm timeout values.");
        std::cout << "New timeouts" << std::endl;
        std::cout << "Timeout interval per individual byte (ms):" << timeouts.ReadIntervalTimeout << std::endl;
        std::cout << "Timeout interval per byte for a read command (ms):" << timeouts.ReadTotalTimeoutMultiplier << std::endl;
        std::cout << "Extra Timeout interval per read command (ms):" << timeouts.ReadTotalTimeoutConstant << std::endl;
        std::cout << "Timeout interval per byte for a wrte command(ms):" << timeouts.WriteTotalTimeoutMultiplier << std::endl;
        std::cout << "Extra Timeout interval per write command (ms)(ms):" << timeouts.WriteTotalTimeoutConstant << std::endl;
        std::cout << std::endl;

        readData(hComm, filename, linesPerTimeRecord);
    }
    catch (std::string err)
    {
        std::cout << err << std::endl;
    }
    catch (...)
    {
        //catch any thrown errors to ensure the port gets closed cleanly below
    }

    CloseHandle(hComm);//Closing the Serial Port

    return 0;
}