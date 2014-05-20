/* DdrescueCmp
** Utility program for cdrom rescue additions to the Gnu program:
**      http://www.gnu.org/software/ddrescue/
**
** This program requires at least one argument, the name of the pair of files named .iso and .log
** that are output from ddrescue. 
**
**  ddrescuecmp ddrfile
**
** When invoked with that one argument, not much happens here-- ddrfile.iso and ddrfile.log
** are read and we print out the number of rescued bytes in ddrfile.iso.
** This is nothing that you can't see better with the ddrescuelog program on same page above.
**
** Add the argument pair "-c ddrfile2" 
**
**  ddrescuecmp ddrfile -c ddrfile2
**
** And this program double-checks the ddrescue results by scanning both ddrfile.iso and ddrfile2.iso
** and checks that all rescued regions in the two files match byte-for-byte.
**
** For file recovery, add the pair "-x Dir"
**
**  ddrescuecmp ddrfile -x dirName
**
**  Ddrescue expects to find the text file dirName.txt and parses it line by line looking for
**  the triplets it needs to extracts files:
**      block number in the iso
**      length of the file in the iso
**      name of the file to extract.
**
**  For each triplet, ddrescuecmp:
**      1 checks the ddrfile.log file for whether the blocks required are rescued. If so, then
**      2 it creates a directory named dirName
**      3 it writes a file using the name in the triplet into that directory, from the corresponding bytes in the iso
**
**  The triplet is parsed using heuristics that come from the use of the isodump utility (linux).
**      A '[' character is expected to preceed the text for the triplet
**      The string ";1" is expected to appear after the triplet
**      NULL bytes in the text file are simply erased (this tends to convert charcters to ASCII)
**      The triplet is white-space separated
**          hex     CDROM block number of file (uints: 2048 byte blocks)
**          len     decimal number of bytes of the file
**          fname   name of the file
**
**  Maybe the CDROM is so garbled, isodump doesn't work and you can't generate triplets with it...
**  The -jpg switch creates a file that has the triplets in it. It scans the iso file assuming the
**  files are contiguous on the CDROM and creates a triplet for every jpg header it finds and that 
**  is wholy contained in good blocks in the ddrescue.  You then have to invoke ddrescuecmp
**  a second time with the -x switch naming the file created with -jpg
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>

static const int CDROM_BLOCK_SIZE = 2048;

typedef unsigned long long AdrType_t;
typedef std::map<AdrType_t, AdrType_t> AdrMap_t;

// these are the triplets that describe files in the ISO
struct FileDesc { 
    AdrType_t pos; 
    AdrType_t len; 
    FileDesc(AdrType_t p, AdrType_t l) : pos(p), len(l) {}
    FileDesc() : pos(0), len(0){}
};
typedef std::map<std::string, FileDesc> FileDescMap_t;

static void readLog(std::ifstream &instr, const std::string &fname, AdrMap_t &res);

int main(int argc, char * argv[])
{
    std::string f1IsoName;
    std::string f2IsoName;
    std::string dirName;
    std::string jpgTextName;
    {
        bool minusC = false;
        bool minusX = false;
        bool minusJpg = false;
        for (int i = 1; i < argc; i++)
        {
            std::string arg = argv[i];
            if (minusC)
            {
                minusC = false;
                f2IsoName = arg;
            }
            else if (minusX)
            {
                minusX = false;
                dirName = arg;
            }
            else if (minusJpg)
            {
                minusJpg = false;
                jpgTextName = arg;
            }
            else if (arg == "-c")
                minusC = true;
            else if (arg == "-x")
                minusX = true;
            else if (arg == "-jpg")
                minusJpg = true;
            else 
            {
                if (f1IsoName.empty())
                    f1IsoName = arg;
                else
                {
                    f1IsoName.clear();
                    break;
                }
            }
        }
        if (minusC || minusX || minusJpg)
            f1IsoName.clear();
    }

    if (f1IsoName.empty())
    {
        std::cerr << "usage: ddrescuecmp <f1> [-c F2] [-x DIR] [-jpg JPG]" << std::endl
            << "  These must exist: <f1>.iso <f1>.log" << std::endl
            << "  and for -c, the files F2.iso F2.log must exist." << std::endl
            << "  and for -x, the file DIR.txt must exist." << std::endl
            << "  DIR.txt is edited from linux utility isodump." << std::endl
            << " -jpg scans <f1>.iso for jpeg file headers and creates the file JPG, which will then work -x" << std::endl;
        return 1;
    }

    std::string f1LogName(f1IsoName);
    std::string f2LogName(f2IsoName);

    std::string dirFileName;
    if (!dirName.empty())
        dirFileName = dirName + ".txt";

    f1IsoName += ".iso";
    f1LogName += ".log";

    if (!f2IsoName.empty())
    {
        f2IsoName += ".iso";
        f2LogName += ".log";
    }

    std::ifstream f1Iso(f1IsoName.c_str(), std::ifstream::binary);
    if (!f1Iso.is_open())
    {
        std::cerr << "Failed to read " << f1IsoName << std::endl;
        return -1;
    }
    std::ifstream f1Log(f1LogName.c_str());
    if (!f1Log.is_open())
    {
        std::cerr << "Failed to read " << f1LogName << std::endl;
        return -1;
    }

    std::ifstream f2Iso;
    std::ifstream f2Log;
    if (!f2IsoName.empty())
    {
        f2Iso.open(f2IsoName.c_str(), std::ifstream::binary);
        if (!f2Iso.is_open())
        {
            std::cerr << "Failed to read " << f2IsoName << std::endl;
            return -1;
        }
        f2Log.open(f2LogName.c_str());
        if (!f2Log.is_open())
        {
            std::cerr << "Failed to read " << f2LogName << std::endl;
            return -1;
        }
    }

    static const AdrType_t BUFSIZE= CDROM_BLOCK_SIZE * 1024;
    char *buf1 = new char [BUFSIZE];
    char *buf2 = new char [BUFSIZE];

    int ret = 0;

    try {
        FileDescMap_t FileDescMap;

        if (!dirFileName.empty())
        {
            std::ifstream ifs(dirFileName.c_str());
            if (!ifs.is_open())
            {
                std::ostringstream oss;
                oss << "Failed to read " << dirFileName << std::endl;
                throw std::runtime_error(oss.str());
            }
            std::string lineBuf;
            while (std::getline(ifs, lineBuf))
            {
                // looking for a block number, length, file name triplet
                std::string::size_type bktPos;
                for ( bktPos = 0; bktPos < lineBuf.size(); )
                {   // assume only ASCII--remove NULL bytes from the line of text
                    if (!lineBuf[bktPos])
                        lineBuf.erase(lineBuf.begin() + bktPos);
                    else
                        bktPos++;
                }
                bktPos = lineBuf.find(']');
                // expect to find a ']' character preceding the good stuff
                if (bktPos != lineBuf.npos)
                {
                    lineBuf = lineBuf.substr(bktPos + 1);
                    // expect to find the string ";1" after the good stuff
                    bktPos = lineBuf.find(";1");
                    if (bktPos != lineBuf.npos)
                    {
                        lineBuf = lineBuf.substr(0, bktPos);
                        AdrType_t addr(0);
                        AdrType_t fileLen(0);
                        char fName[256];
                        // the triplet is
                        //      hex     (CDROM block number--2048 byte blocks0
                        //      dec     (File length in decimal bytes)
                        //      fileName    (name of the file)
                        sscanf(lineBuf.c_str(), " %x %d %*s %255s", &addr, &fileLen, fName);
                        addr *= CDROM_BLOCK_SIZE;
                        FileDescMap[fName] = FileDesc(addr, fileLen);
                    }
                }
            }
        }

        AdrMap_t f1Map;
        AdrMap_t f2Map;

        readLog(f1Log, f1IsoName, f1Map);

        // and second ddrescue output, for -c
        if (f2Log.is_open())
            readLog(f2Log, f2IsoName, f2Map);

        for (AdrMap_t::const_iterator i1 = f1Map.begin();
            i1 != f1Map.end();
            i1 ++)
        {
            AdrType_t f1First = i1->first;
            AdrType_t f1Last = f1First + i1->second;
            AdrMap_t::const_iterator i2 = f2Map.lower_bound(i1->first); // first entry not before
            if (i2 != f2Map.begin()) 
                i2--;    // back up one.

            for (; i2 != f2Map.end(); i2++)
            {
                AdrType_t f2First = i2->first;
                AdrType_t f2Last = f2First + i2->second;

                if (f2First >= f1Last)
                    break;

                if ((f2First < f1Last) && (f2Last > f1First))
                {
                    AdrType_t begin = std::max(f1First, f2First);
                    AdrType_t end = std::min(f1Last, f2Last);
                    std::cout << "Overlap starting 0x" << std::hex << begin << 
                        " of length 0x" << std::hex << (end - begin) << std::endl;
                    while (end > begin)
                    {
                        f1Iso.seekg(begin);
                        if (f1Iso.eof())
                            throw std::runtime_error("oops cannot seek f1Iso");
                        f2Iso.seekg(begin);
                        if (f2Iso.eof())
                            throw std::runtime_error("oops cannot seek f2Iso" );
                        AdrType_t readLen = std::min(BUFSIZE, end - begin);
                        f1Iso.read(buf1, readLen);
                        std::streamsize c = f1Iso.gcount();
                        if (c != readLen)
                            throw std::runtime_error( "oops cannot read f1Iso" );
                        f2Iso.read(buf2, readLen);
                        c = f2Iso.gcount();
                        if (c != readLen)
                            throw std::runtime_error( "oops cannot read f2Iso");

                        const long *v1 = reinterpret_cast<long *>(buf1);
                        const long *v2 = reinterpret_cast<long *>(buf2);
                        for (AdrType_t i = 0; i < readLen/sizeof(long); i++)
                        {
                            if (*v1++ != *v2++)
                            {
                                std::ostringstream oss;
                                oss << "Oops. Files do not match at 0x" << std::hex << (begin + i * sizeof(long)) ;
                                throw std::runtime_error(oss.str());
                            }
                        }
                        begin += readLen;
                    }
                }
            }
        }

        // reduce the map to its smallest representation
        // ddrescue never seems to have this redudancy in its log file output
        for (AdrMap_t::iterator itor = f1Map.begin();
            itor != f1Map.end();
            itor ++)
        {
            AdrMap_t::iterator iNext = itor;
            iNext ++;
            if (iNext != f1Map.end())
            {
                AdrType_t f1First = itor->first + itor->second;
                AdrType_t f1Last = iNext->first;
                if (f1First == f1Last)
                {
                    itor->second += iNext->second;
                    f1Map.erase(iNext);
                }
            }
        }

        // process -x 
        bool createdDir = false;
        for (
            FileDescMap_t::const_iterator fileItor = FileDescMap.begin();
            fileItor != FileDescMap.end();
            fileItor++)
        {
            AdrMap_t::const_iterator i1 = f1Map.lower_bound(fileItor->second.pos);
            if (i1 != f1Map.begin())
            {
                i1--;
                const AdrType_t f1First = i1->first;
                const AdrType_t f1Last = f1First + i1->second;
                AdrType_t fiFirst = fileItor->second.pos;
                AdrType_t fiLast = fileItor->second.pos + fileItor->second.len;
                if ((fiFirst >= f1First) && (fiLast <= f1Last))
                {
                    if (!createdDir)
                    {
                        std::string cmd = "mkdir ";
                        cmd += dirName;
                        ::system(cmd.c_str());
                        createdDir = true;
                    }
                    std::ofstream out((dirName + "/" + fileItor->first).c_str(), std::ofstream::binary);
                    if (!out.is_open())
                    {
                        throw std::runtime_error(std::string("Cannot create ") + fileItor->first );
                    }
                    else
                    {
                        for (; fiFirst < fiLast;)
                        {
                            f1Iso.clear();
                            f1Iso.seekg(fiFirst);
                            AdrType_t toRead = std::min(fiLast - fiFirst, BUFSIZE);
                            f1Iso.read(buf1, toRead);
                            if (f1Iso.gcount() != toRead)
                                throw std::runtime_error(std::string("Oops failed to read ") + fileItor->first);
                            out.write(buf1, toRead);
                            fiFirst += toRead;
                        }
                        std::cout << "Extracted file " << fileItor->first << std::endl;
                    }
                }
                else
                    std::cout << "Missing data for " << fileItor->first << " can't extract." << std::endl;
            }
        }

        // process -jpeg 
        // scan the iso for jpeg files 
        if (!jpgTextName.empty())
        {
            std::ofstream ofs(jpgTextName.c_str());
            if (!ofs.is_open())
                throw std::runtime_error(std::string("Could not open ") + jpgTextName);
            int fileCount = 0;
            for (AdrMap_t::const_iterator itor = f1Map.begin();
                itor != f1Map.end();
                itor ++)
            {
                enum {NO_FILE, IN_PROGRESS, FOUND_0XFF, 
                    FOUND_BC0, FOUND_BC1, SKIPPING, COMPLETE} jpgfileInProgress(NO_FILE);
                AdrType_t f1First = itor->first;
                AdrType_t f1Last = f1First + itor->second;

                long jpgFileLength = 0;
                long jpgFileBlockNum = 0;
                int skipping = 0;
                while (f1First < f1Last)
                {
                    f1Iso.clear();
                    f1Iso.seekg(f1First);
                    const AdrType_t toRead = std::min(f1Last - f1First, BUFSIZE);
                    f1Iso.read(buf1, toRead);
                    if (f1Iso.gcount() != toRead)
                    {
                        std::ostringstream oss;
                        oss << "Failed to read bytes from " << f1IsoName << " at " << 
                            std::hex << f1First << " length " << std::hex << toRead;
                        throw std::runtime_error(oss.str());
                    }
                    int numBlocks = (int)(toRead/CDROM_BLOCK_SIZE);
                    for (int i = 0; i < numBlocks; i++)
                    {
                        const unsigned char *block = reinterpret_cast<const unsigned char *>(&buf1[i * CDROM_BLOCK_SIZE]);
                        const unsigned char *p = block;
                        while (p - block < CDROM_BLOCK_SIZE)
                        {
                            switch (jpgfileInProgress)
                            {
                            case NO_FILE:
                                if ((*p++ == 0xFF) && (*p++ == 0xD8) && (*p++ == 0xFF) && (*p++ == 0xE0))
                                {
                                    /* this is the only case in the switch where we can assume *p 
                                    ** is within the current buf1. All the other cases have to deal
                                    ** with the possiblity that *p is at the end of a CDROM block.
                                    ** But jpeg file headers will only appear at the beginning of a file,
                                    ** which, on a CDROM, will only appear at the beginning of a 2048 byte 
                                    ** block. */
                                    p += 2;
                                    if (strcmp((const char *)p, "JFIF") == 0)
                                    {
                                        int blockNumNow = (int)(f1First/CDROM_BLOCK_SIZE);
                                        if (f1First % CDROM_BLOCK_SIZE)
                                        {
                                            std::ostringstream oss;
                                            oss << "oops: ddrescue block not on CDROM block size boundary: " 
                                                << std::hex << f1First;
                                            throw std::runtime_error(oss.str());
                                        }
                                        int blockSize = (block[5] & 0xFF) | ((block[4] << 8) & 0xFF00);
                                        // reset our pointer to end of this block
                                        p = &block[blockSize + 4];
                                        blockNumNow += i;
                                        if (blockNumNow == 0x1159)
                                            jpgFileLength = 0;
                                        jpgFileBlockNum = blockNumNow;
                                        jpgFileLength = 0;
                                        jpgfileInProgress = IN_PROGRESS;
                                        fileCount += 1;
                                        std::cout << "jpeg header at block number " << std::hex << blockNumNow << std::endl;
                                    }
                                }
                                break;

                            case IN_PROGRESS:
                                if (*p++ == 0xFF)
                                    jpgfileInProgress = FOUND_0XFF;
                                break;

                            case FOUND_0XFF:
                                {
                                    unsigned char c = *p++;
                                    if (c == 0xD9)
                                        jpgfileInProgress = COMPLETE;
                                    else if (c == 0xFF); // do nothing
                                    else if (c == 0)
                                        jpgfileInProgress = IN_PROGRESS;
                                    // http://en.wikipedia.org/wiki/JPEG
                                    // http://www.fileformat.info/format/jpeg/egff.htm
                                    else if (((c & 0xF0) == 0xd0) &&
                                          ((c & 0x0F) <= 7))
                                    {
                                        jpgfileInProgress = IN_PROGRESS; 
                                    }
                                    else
                                    {
                                        unsigned char top4 = c & 0xF0;
                                        if ((top4 == 0xC0) ||
                                            (top4 == 0xD0) ||
                                            (top4 == 0xE0) ||
                                            (top4 == 0xF0))
                                        {
                                            jpgfileInProgress = FOUND_BC0;
                                            skipping = 0;
                                        }
                                        else
                                        {
                                            jpgfileInProgress = IN_PROGRESS;
                                        }
                                    }
                                }
                                break;

                            case FOUND_BC0:
                                skipping += *p++;
                                jpgfileInProgress = FOUND_BC1;
                                break;

                            case FOUND_BC1:
                                skipping <<= 8;
                                skipping |= *p++;
                                skipping -= 2;
                                if (skipping > 0)
                                    jpgfileInProgress = SKIPPING;
                                else
                                    jpgfileInProgress = IN_PROGRESS; // tests don't get here
                                break;

                            case SKIPPING:
                                p++;
                                skipping -= 1;
                                if (skipping <= 0)
                                    jpgfileInProgress = IN_PROGRESS;
                                break;

                            }
                            if (jpgfileInProgress == COMPLETE)
                            {
                                jpgfileInProgress = NO_FILE;
                                jpgFileLength += static_cast<int>(p - block);
                                ofs << "] " << std::hex << jpgFileBlockNum << " " << std::dec << 
                                    jpgFileLength << " 00/ File" << 
                                    fileCount << ".jpg;1" << std::endl;
                                break;  // file can only start on block boundary
                            }
                            else if (jpgfileInProgress == NO_FILE)
                                break;
                        }
                        jpgFileLength += CDROM_BLOCK_SIZE;
                    }
                    f1First += toRead;
               }
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        ret = -1;
    }

    delete [] buf1;
    delete [] buf2;
    return ret;
}

// parse the ddrescue log file for what we want from it.
void readLog(std::ifstream &instr, const std::string &fname, AdrMap_t &res)
{
    std::string lineBuf;
    bool skippedOne = false;
    AdrType_t totalBytes(0);
    while (std::getline(instr, lineBuf))
    {
        if (lineBuf.empty()) continue;
        if (lineBuf[0] == '#') continue;
        if (!skippedOne) { skippedOne = true; continue; }
        AdrType_t addr(0);
        AdrType_t len(0);
        char stat('-');
        if (sscanf(lineBuf.c_str(), "%lx %lx %c", &addr, &len, &stat) != 3)
            continue;
        if (stat != '+') continue;

        if (len == 0)
        {
            std::ostringstream oss;
            oss << "oops got zero length in log \"" << lineBuf << "\"";
            throw std::runtime_error(oss.str());
        }

        if (!res.empty())
        {
            AdrMap_t::iterator itor = res.lower_bound(addr);    // first entry not before

            if (itor != res.end())
            {
                if (itor->first <= (addr + len))
                {
                    std::ostringstream oss;
                    oss << "oops overlap of existing 0x" << std::hex << itor->first << 
                        " 0x" << std::hex << itor->second << 
                        " with 0x" << std::hex << addr << " 0x" <<  std::hex << len;
                    throw std::runtime_error(oss.str());
                }
            }

            if (itor != res.begin())
            {
                itor--;
                if (addr <= itor->first + itor->second)
                {
                    std::ostringstream oss;
                    oss << "oops overlap of existing 0x" << std::hex << itor->first << 
                        " 0x" << std::hex << itor->second << 
                        " with 0x" << std::hex << addr << " 0x" << std::hex << len;
                    throw std::runtime_error(oss.str());
                }
            }
        }

        res[addr] = len;
        totalBytes += len;
    }
    instr.close();
    std::cout << "Total bytes rescued in \"" << fname << "\" " << totalBytes << std::endl;
}