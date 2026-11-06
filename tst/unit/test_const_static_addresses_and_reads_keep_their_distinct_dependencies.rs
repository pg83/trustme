struct Node {
    value: u8,
    next: *const Node,
    empty: [u8; 0],
}
unsafe impl Sync for Node {}
static NODE: Node = Node { value: 41, next: &NODE, empty: [] };
struct Handle(*const Node);
unsafe impl Sync for Handle {}
static HANDLE: Handle = Handle(&NODE);
struct Indirect(*const Handle);
unsafe impl Sync for Indirect {}
static INDIRECT: Indirect = Indirect(&HANDLE);

const ADDRESS: *const Node = &NODE;
const DIRECT: u8 = NODE.value;
const THROUGH: u8 = unsafe { (*HANDLE.0).value };
const BORROWED: &u8 = unsafe { &(*(*INDIRECT.0).0).value };
const EMPTY: &[u8; 0] = &NODE.empty;
const OFFSET: isize = unsafe {
    (std::ptr::addr_of!(NODE.empty) as *const u8)
        .offset_from(std::ptr::addr_of!(NODE) as *const u8)
};
const COPIED: [u8; 0] = {
    let mut output = [0u8; 0];
    unsafe {
        std::ptr::copy_nonoverlapping(
            std::ptr::addr_of!(NODE.empty) as *const u8,
            output.as_mut_ptr(),
            0,
        );
    }
    output
};
fn main() {
    assert_eq!(ADDRESS, std::ptr::addr_of!(NODE));
    assert_eq!(NODE.next, ADDRESS);
    assert_eq!((DIRECT, THROUGH, *BORROWED), (41, 41, 41));
    assert_eq!(EMPTY.len(), 0);
    assert_eq!(COPIED, []);
    assert_eq!(OFFSET as usize, std::mem::offset_of!(Node, empty));
}
