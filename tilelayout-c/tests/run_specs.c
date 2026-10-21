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
    return spec_report() == 0 ? 0 : 1;
}
