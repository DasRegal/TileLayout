#include "tl_expansion_joint.h"

#define STRIP_END_MARGIN 5.0 /* мм за каждым концом */

TlExpansionJoint *tl_expansion_joint_new(TlVec2 start, TlVec2 end, double gap)
{
    TlExpansionJoint *joint = TL_NEW(TlExpansionJoint);
    joint->start = start;
    joint->end = end;
    joint->gap = gap;
    return joint;
}

void tl_expansion_joint_free(TlExpansionJoint *joint) { free(joint); }

double tl_expansion_joint_length(const TlExpansionJoint *joint) { return tl_distance(joint->start, joint->end); }

void tl_expansion_joint_translate(TlExpansionJoint *joint, TlVec2 offset)
{
    joint->start = tl_vec2_add(joint->start, offset);
    joint->end = tl_vec2_add(joint->end, offset);
}

TlVec2 *tl_expansion_joint_end_point(TlExpansionJoint *joint, TlExpansionJointEnd which)
{
    return which == TL_EXPANSION_JOINT_START ? &joint->start : &joint->end;
}

TlPolygon tl_expansion_joint_strip(const TlExpansionJoint *joint)
{
    TlVec2 direction = tl_vec2_normalized(tl_vec2_sub(joint->end, joint->start));
    TlVec2 half_gap = tl_vec2_scale(tl_vec2_perpendicular(direction), joint->gap / 2);
    TlVec2 a = tl_vec2_sub(joint->start, tl_vec2_scale(direction, STRIP_END_MARGIN));
    TlVec2 b = tl_vec2_add(joint->end, tl_vec2_scale(direction, STRIP_END_MARGIN));
    const TlVec2 corners[] = {tl_vec2_add(a, half_gap), tl_vec2_add(b, half_gap), tl_vec2_sub(b, half_gap),
                              tl_vec2_sub(a, half_gap)};
    return tl_polygon_from_points(corners, 4);
}
