#include <openvdb/openvdb.h>
#include <openvdb/tools/LevelSetSphere.h>

using namespace std::chrono_literals;

int main() {
    putenv("MALLOCVIS=layout:address;show_text:0");
    openvdb::initialize();

    // create sdf square
    openvdb::FloatGrid::Ptr grid = openvdb::tools::createLevelSetSphere<openvdb::FloatGrid>(1.0f, openvdb::Vec3f(0,0,0), 0.1f);
    for (int i = 0; i < 40; ++i) {
        std::this_thread::sleep_for(1ms);
        grid = grid->deepCopy();
        grid = grid->deepCopy();
        grid = openvdb::tools::createLevelSetSphere<openvdb::FloatGrid>(1.0f, openvdb::Vec3f(0,0,0), 0.1f);
    }

    // save grid to file
    openvdb::io::File file("/tmp/sphere.vdb");
    openvdb::GridPtrVec grids;
    grids.push_back(grid);
    file.write(grids);
    file.close();

    return 0;
}
