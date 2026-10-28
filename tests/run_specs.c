#include "spec.h"
#include "specs.h"

int main(void)
{
    spec_geometry();
    spec_regions();
    spec_entities();
    spec_project();
    spec_layout();
    spec_summary();
    spec_storage();
    spec_ruler();
    spec_alignment();
    spec_partition_shape();
    spec_nodes();
    spec_catalog();
    spec_expansion_joint();
    spec_doorway();
    spec_web();
    spec_commands();
    spec_drag();
    spec_pattern();
    spec_estimate();
    spec_pipes();
    spec_zones();
    spec_walls();
    spec_panno();
    spec_areas();
    spec_fixtures();
    spec_drains();
    spec_cutmap();
    spec_dxf();
    spec_underlay();
    return spec_report() == 0 ? 0 : 1;
}
