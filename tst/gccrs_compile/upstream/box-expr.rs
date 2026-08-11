struct NonCopyStruct { id: i32 }
impl NonCopyStruct { fn get_id(&self) -> i32 { self.id } }
fn main() {
    let first = Box::new(NonCopyStruct { id: 42 });
    let _moved = *first;
    let second = Box::new(NonCopyStruct { id: 100 });
    let _field = second.id;
    let _method = second.get_id();
}
