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
    return spec_report() == 0 ? 0 : 1;
}
