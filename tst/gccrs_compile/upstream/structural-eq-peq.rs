#[derive(PartialEq, Eq)]
struct StructuralValue { field: i32 }

pub fn compare() -> bool {
    StructuralValue { field: 1 } == StructuralValue { field: 1 }
}
