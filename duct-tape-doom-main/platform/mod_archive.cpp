#include "mod_archive.h"
#include "miniz.h"
#include <algorithm>
#include <map>
#include <stdexcept>
namespace doommod {
struct Archive::Impl {
    mz_zip_archive zip{};
    std::map<std::string,mz_uint> entries;
    ~Impl(){if(zip.m_pState)mz_zip_reader_end(&zip);}
};
std::string Archive::normalize(const std::string &name) {
    if(name.empty()||name.size()>4096)throw std::runtime_error("Empty or oversized PK3 resource name");
    std::string out;out.reserve(name.size());
    for(unsigned char c:name) {
        if(c<32 || c==127 || c==':' || c=='\\')throw std::runtime_error("Invalid PK3 resource path: "+name);
        out.push_back(c>='A'&&c<='Z'?char(c+'a'-'A'):char(c));
    }
    size_t start=0;
    do {
        size_t end=out.find('/',start);
        std::string part=out.substr(start,end==std::string::npos?end:end-start);
        if(part.empty()||part=="."||part=="..")throw std::runtime_error("Non-canonical PK3 resource path: "+name);
        if(end==std::string::npos)break;
        start=end+1;
    }while(start<out.size());
    if(out.back()=='/')throw std::runtime_error("Resource is a directory: "+name);
    return out;
}
Archive::Archive(const std::string &path):impl(new Impl) {
    if(!mz_zip_reader_init_file(&impl->zip,path.c_str(),0))throw std::runtime_error("Cannot open PK3: "+path);
    auto count=mz_zip_reader_get_num_files(&impl->zip);
    if(count>100000)throw std::runtime_error("PK3 has too many entries");
    uint64_t total=0;
    for(mz_uint i=0;i<count;++i) {
        mz_zip_archive_file_stat st;
        if(!mz_zip_reader_file_stat(&impl->zip,i,&st))throw std::runtime_error("Cannot read PK3 directory");
        if(st.m_is_directory)continue;
        mz_uint length=mz_zip_reader_get_filename(&impl->zip,i,nullptr,0);
        if(length<2||length>4097)throw std::runtime_error("PK3 resource name too long");
        std::vector<char> name(length);
        if(mz_zip_reader_get_filename(&impl->zip,i,name.data(),length)!=length)throw std::runtime_error("Cannot read PK3 resource name");
        if(std::find(name.begin(),name.end()-1,'\0')!=name.end()-1)throw std::runtime_error("PK3 resource contains a NUL");
        auto key=normalize(name.data());
        if(st.m_uncomp_size>64u*1024u*1024u||total>512u*1024u*1024u-st.m_uncomp_size)
            throw std::runtime_error("PK3 exceeds resource size limit");
        total+=st.m_uncomp_size;
        if(st.m_is_encrypted||!mz_zip_reader_is_file_supported(&impl->zip,i))throw std::runtime_error("Encrypted or unsupported PK3 entry: "+key);
        // Match the load order rule: the last duplicate wins.
        impl->entries[key]=i;
    }
}
Archive::~Archive()=default;
bool Archive::contains(const std::string &name) const {return impl->entries.count(normalize(name))!=0;}
std::vector<uint8_t> Archive::read(const std::string &name) const {
    auto key=normalize(name);
    auto it=impl->entries.find(key);
    if(it==impl->entries.end())throw std::runtime_error("Missing PK3 resource: "+name);
    mz_zip_archive_file_stat st;
    if(!mz_zip_reader_file_stat(&impl->zip,it->second,&st))throw std::runtime_error("Cannot read PK3 resource metadata");
    std::vector<uint8_t> data(static_cast<size_t>(st.m_uncomp_size));
    uint8_t dummy=0;
    if(!mz_zip_reader_extract_to_mem(&impl->zip,it->second,data.empty()?&dummy:data.data(),data.size(),0))
        throw std::runtime_error("PK3 decompression/CRC failed: "+name);
    return data;
}
std::string Archive::text(const std::string &name) const {
    auto data=read(name);
    if(std::find(data.begin(),data.end(),0)!=data.end())throw std::runtime_error("Binary content in text resource: "+name);
    return std::string(data.begin(),data.end());
}
std::vector<std::string> Archive::names() const {
    std::vector<std::string> result;result.reserve(impl->entries.size());
    for(const auto &e:impl->entries)result.push_back(e.first);
    return result;
}
}
