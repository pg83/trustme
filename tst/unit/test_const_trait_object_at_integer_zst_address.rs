trait Value { fn value(&self) -> u32; }
impl Value for () { fn value(&self) -> u32 { 47 } }
#[repr(align(64))]
struct Aligned;
impl Value for Aligned { fn value(&self) -> u32 { 89 } }
trait Other { fn other(&self) -> u32; }
trait Combined: Value + Other {}
impl Other for () { fn other(&self) -> u32 { 103 } }
impl Combined for () {}
const COMBINED: &dyn Combined = unsafe { &*(1usize as *const ()) };
const UPCAST: &dyn Other = COMBINED;
const RAW: *const dyn Other = COMBINED as *const dyn Combined;
const NULL: *const dyn Value = 0usize as *const ();
const UNIT: &dyn Value = unsafe { &*(1usize as *const ()) };
const ALIGNED: &(dyn Value + Sync) = unsafe { &*(64usize as *const Aligned) };
const AUTO: &dyn Sync = ALIGNED;
fn main() {
    assert_eq!(UNIT.value(), 47);
    assert!((NULL as *const ()).is_null());
    assert_eq!(UPCAST.other(), 103);
    assert_eq!(unsafe { (&*RAW).other() }, 103);
    assert_eq!(std::mem::align_of_val(AUTO), 64);
    assert_eq!(std::mem::size_of_val(UNIT), 0);
    assert_eq!(std::mem::align_of_val(UNIT), 1);
    assert_eq!(ALIGNED.value(), 89);
    assert_eq!(std::mem::size_of_val(ALIGNED), 0);
    assert_eq!(std::mem::align_of_val(ALIGNED), 64);
}
