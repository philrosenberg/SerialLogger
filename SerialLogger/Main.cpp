#include<windows.h>
#include<stdio.h>
#include<iostream>
#include<string>
#include<sstream>
#include<fstream>
#include<svector/time.h>
#include<svector/Units.h>
#include<svector/sstring.h>
#include<deque>
#include<thread>
#include<conio.h>

//this class is a fifo buffer. It can cope with one thread pushing and one
//thread popping, in that it won't cause a race. No resize is possible as this
//would break multithreading. If the end overtakes the begin then the first
//loop of data will be lost and the fifo will think is is just a few elements
//long. You must check size is non-zero before popping
template<class T>
class Fifo
{
public:
    Fifo(size_t size)
        :m_buffer(size)
    {
        m_begin = m_buffer.begin();
        m_end = m_buffer.begin();
    }
    void push_back(const T& val)
    {
        *m_end = val;
        increment(m_end);
    }
    T pop_front()
    {
        typename std::vector<T>::iterator pointerToReturn = m_begin;
        increment(m_begin);
        return *pointerToReturn;
    }
    size_t size() const
    {
        //freeze the pointers we will use in case of multithreading
        typename std::vector<T>::iterator begin = m_begin;
        typename std::vector<T>::iterator end = m_end;
        if (end >= begin)
            return size_t(end - begin);
        return size_t((m_buffer.end() - end) + (begin - m_buffer.begin()));

    }
private:
    void increment(typename std::vector<T>::iterator& iter)
    {
        ++iter;
        if (iter == m_buffer.end())
            iter = m_buffer.begin();
    }
    std::vector<T> m_buffer;
    typename std::vector<T>::iterator m_begin;
    typename std::vector<T>::iterator m_end;
};

const char lineEnd[2] = { 13,10 };

void throwWindowsError(std::string str)
{
    auto code = GetLastError();
    std::ostringstream WinError;
    WinError << "Windows error code: " << code;
    throw(str + " " + WinError.str());
}

void writeData(std::wstring folder, std::wstring filenameBase, Fifo<unsigned char> *dataBuffer, size_t linesPerFile, bool outputToScreen, bool *stop)
{
    std::wostringstream filename;
    size_t fileNumber = 0;
    std::wstring dateString = sci::nativeUnicode(sci::utf8ToUtf16(sci::UtcTime::now().getIso8601String(0, true, true, true)));
    filename << folder << dateString << L"_" << std::setw(10) << std::setfill(L'0') << fileNumber << filenameBase;
    std::fstream fout;
    fout.open(filename.str(), std::ios::out);
    if (!fout.is_open())
    {
        std::cout << "Failed to open data file." << std::endl;
        return;
    }
    fout << "PAMB0001";
    fout << lineEnd;

    unsigned char singleByte;
    size_t lineNumber = 0;
    while (dataBuffer->size() > 0 || !(*stop)) //run until told to stop and the buffer is emptied
    {
        if (dataBuffer->size() > 0)
        {
            singleByte = dataBuffer->pop_front();
            fout.write((char*)&singleByte, 1);
            if (outputToScreen)
                std::cout.write((char*)&singleByte, 1);
            if (singleByte == 0x0d)
            {
                ++lineNumber;
                if (lineNumber == linesPerFile)
                {
                    lineNumber = 0;
                    ++fileNumber;
                    fout.close();
                    fout.clear();
                    filename.str(L"");
                    filename.clear();
                    filename << folder << dateString << L"_" << std::setw(10) << std::setfill(L'0') << fileNumber << filenameBase;
                    fout.open(filename.str(), std::ios::out);
                }
            }
        }
        else
            fout.flush();//if we have nothing else to do we may as well flush the buffer
    }
}


void readData(HANDLE hComm, Fifo<unsigned char> *dataBuffer, size_t linesPerTimeRecord, bool writeTimestampsToScreen, bool* stop)
{
    size_t linesSinceLastTimeRecord = 0;
    FILETIME timeRecord{ 0,0 };

    sci::UtcTime begin = sci::UtcTime::now();

    unsigned char singleByte;
    bool getTimestampAtNextByte = true;
    std::ostringstream timeStream;
    while (!(*stop))
    {
        DWORD nRead;
        if (ReadFile(hComm, &singleByte, 1, &nRead, NULL) && nRead == 1)
        {
            if (getTimestampAtNextByte)
            {
                GetSystemTimePreciseAsFileTime(&timeRecord);

                timeStream << "tr " << timeRecord.dwHighDateTime << " " << timeRecord.dwLowDateTime << "\n";

                for (char& c : timeStream.str())
                    dataBuffer->push_back((unsigned char)c);

                if (writeTimestampsToScreen)
                    std::cout << timeStream.str();

                timeStream.str("");
                timeStream.clear();

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
            dataBuffer->push_back(singleByte);
            if (writeTimestampsToScreen && linesSinceLastTimeRecord == 0)
            {
                if (singleByte == 0x0d)
                    std::cout << "\n";
                else
                    std::cout.write((char*)(&singleByte), 1);
            }
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

    std::wstring comport;
    std::wstring filename = L"C:\\Temp\\serialDataTemp.txt";
    size_t linesPerTimeRecord = size_t(-1);
    bool outputToScreen = true;
    if (argc == 2)
    {
        std::wstring comport = L"\\\\.\\" + std::wstring(argv[1]);
    }
    else if (argc == 4)
    {
        comport = L"\\\\.\\" + std::wstring(argv[1]);
        filename = argv[2];
        outputToScreen = false;

        wchar_t* end = nullptr;
        linesPerTimeRecord = std::wcstoul(argv[3], &end, 10);
        if (end == argv[3])
        {
            std::wcout << "could not convert the third argument to an integer" << std::endl;
            return 1;
        }
    }
    else
    {
        std::cout << "please enter com port and filename and lines per time record as arguments, e.g.\nSerialLogger COM4 \"C:\\Data\\my file.txt\" 400";
        std::cout << "or for display mode just enter the com port, e.g \nSerialLogger COM4";
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

        std::cout << "About to begin logging - enter q to quit.\n\n\n";

        Fifo<unsigned char> dataBuffer(10 * 1024 * 1024); //10 MB
        bool stop = false;

        //put around 100,000 lines in each file
        size_t linesPerFile = (100000 / (linesPerTimeRecord + 1)) * (linesPerTimeRecord + 1);
        if (linesPerTimeRecord == 0)
            linesPerFile = linesPerTimeRecord + 1;
        //split the filename into the folder and file part
        std::wstring folder;
        std::wstring file = filename;
        size_t lastSlash = filename.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            folder = filename.substr(0, lastSlash + 1);
            file = filename.substr(lastSlash + 1);
        }

        std::thread readThread(readData, hComm, &dataBuffer, linesPerTimeRecord, true, &stop);
        std::thread writeThread(writeData, folder, file, &dataBuffer, linesPerFile, outputToScreen, &stop);

        std::string input;
        while (1)
        {
            std::cin >> input;
            if (input == "q" || input == "Q")
                break;
            else
                std::cout << "Unrecognised input - enter q to quit.\n\n\n";
        }
        stop = true;
        readThread.join();
        writeThread.join();
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