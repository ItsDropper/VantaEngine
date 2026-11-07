#include "vanta/renderer/ModelAssetStore.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
using namespace vanta::renderer;
struct TestCase{std::string_view name;bool(*run)();};
const auto kDirectory=std::filesystem::temp_directory_path()/"vanta_asset_store_tests";
const auto kModelPath=kDirectory/"simple.obj";

bool WriteValidModel()
{
    std::error_code error;
    std::filesystem::create_directories(kDirectory,error);
    if(error)return false;
    std::ofstream file(kModelPath,std::ios::binary);
    if(!file)return false;
    file<<"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    return static_cast<bool>(file);
}

bool loadsAndCachesObjByCanonicalPath()
{
    if(!WriteValidModel())return false;
    ModelAssetStore store;
    ModelHandle first{},second{};
    std::string error;
    if(!store.LoadModel(kModelPath,first,error)||!store.LoadModel(kDirectory/"."/ "simple.obj",second,error))
        return false;
    const CpuModel* model=store.GetModel(first);
    const auto* source=store.GetSourcePath(first);
    return first==second&&store.LoadedCount()==1&&model!=nullptr&&
           model->vertices.size()==3&&model->indices.size()==3&&
           model->materials.size()==1&&model->submeshes.size()==1&&
           source!=nullptr&&*source==std::filesystem::canonical(kModelPath);
}

bool unloadInvalidatesHandleAndAllowsReload()
{
    if(!WriteValidModel())return false;
    ModelAssetStore store;
    ModelHandle first{},second{};
    std::string error;
    if(!store.LoadModel(kModelPath,first,error)||!store.Unload(first)||
       store.Contains(first)||store.GetModel(first)!=nullptr||store.LoadedCount()!=0)
        return false;
    if(!store.LoadModel(kModelPath,second,error))return false;
    return second.IsValid()&&second.index==first.index&&
           second.generation!=first.generation&&store.Contains(second);
}

bool clearInvalidatesAllHandles()
{
    if(!WriteValidModel())return false;
    ModelAssetStore store;
    ModelHandle first{},second{};
    std::string error;
    if(!store.LoadModel(kModelPath,first,error))return false;
    const auto otherPath=kDirectory/"other.obj";
    {
        std::ofstream file(otherPath);
        file<<"v 0 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\n";
    }
    if(!store.LoadModel(otherPath,second,error))return false;
    store.Clear();
    const bool cleared=!store.Contains(first)&&!store.Contains(second)&&store.LoadedCount()==0;
    std::error_code ignored;
    std::filesystem::remove_all(kDirectory,ignored);
    return cleared;
}

bool unsupportedAndMissingPathsReportErrors()
{
    ModelAssetStore store;
    ModelHandle handle{};
    std::string error;
    const auto unsupported=kDirectory/"mesh.fbx";
    {
        std::error_code ignored;
        std::filesystem::create_directories(kDirectory,ignored);
        std::ofstream file(unsupported);
        file<<"not a supported model";
    }
    const bool unsupportedRejected=!store.LoadModel(unsupported,handle,error)&&!error.empty()&&!handle.IsValid();
    error.clear();
    const bool missingRejected=!store.LoadModel(kDirectory/"missing.obj",handle,error)&&
                               !error.empty()&&!handle.IsValid();
    std::error_code ignored;
    std::filesystem::remove_all(kDirectory,ignored);
    return unsupportedRejected&&missingRejected;
}

bool malformedModelDoesNotEnterCache()
{
    std::error_code ignored;
    std::filesystem::create_directories(kDirectory,ignored);
    const auto invalid=kDirectory/"invalid.obj";
    {
        std::ofstream file(invalid);
        file<<"v 0 0 0\nf 1 2 3\n";
    }
    ModelAssetStore store;
    ModelHandle handle{};
    std::string error;
    const bool rejected=!store.LoadModel(invalid,handle,error)&&!error.empty()&&
                        !handle.IsValid()&&store.LoadedCount()==0;
    std::filesystem::remove_all(kDirectory,ignored);
    return rejected;
}

constexpr std::array kCases{
    TestCase{"loads and caches OBJ by canonical path",loadsAndCachesObjByCanonicalPath},
    TestCase{"unload invalidates handles and supports reload",unloadInvalidatesHandleAndAllowsReload},
    TestCase{"clear invalidates every loaded handle",clearInvalidatesAllHandles},
    TestCase{"unsupported and missing paths report errors",unsupportedAndMissingPathsReportErrors},
    TestCase{"malformed model never enters the cache",malformedModelDoesNotEnterCache}
};
}

int main()
{
    int failures=0;
    for(const auto& test:kCases)
    {
        if(!test.run()){std::cerr<<"FAIL: "<<test.name<<'\n';++failures;}
        else std::cout<<"PASS: "<<test.name<<'\n';
    }
    std::cout<<(kCases.size()-static_cast<std::size_t>(failures))
             <<'/'<<kCases.size()<<" asset-store tests passed.\n";
    return failures==0?0:1;
}
