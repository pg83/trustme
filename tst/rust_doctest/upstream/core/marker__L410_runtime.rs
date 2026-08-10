// Extracted from library/core/src/marker.rs:410
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    struct PointList;
    #[derive(Copy, Clone)]
    struct PointListWrapper<'a> {
        point_list_ref: &'a PointList,
    }
}
