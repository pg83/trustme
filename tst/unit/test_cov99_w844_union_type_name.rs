union CovUnion {
    integer: u32,
    real: f32,
}

fn main() {
    assert!(std::any::type_name::<CovUnion>().ends_with("::CovUnion"));
}
