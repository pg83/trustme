/* std's `readlink`: `buf` is `Vec::with_capacity(..)` with no element type until
   `from_vec(buf)` at the end; `buf.as_mut_ptr() as *mut _` goes to a `*mut c_char`
   parameter.  Upstream checks the cast last (`CastCheck`), so it decides nothing
   about the element type; only the later use says `u8`. */
unsafe fn fill(_p: *const i8, buf: *mut i8, len: usize) -> isize {
    for i in 0..len {
        *buf.add(i) = b'a' as i8;
    }
    len as isize
}

trait IsMinusOne {
    fn is_minus_one(&self) -> bool;
}

impl IsMinusOne for isize {
    fn is_minus_one(&self) -> bool {
        *self == -1
    }
}

fn cvt<T: IsMinusOne>(t: T) -> Result<T, ()> {
    if t.is_minus_one() { Err(()) } else { Ok(t) }
}

fn from_vec(v: Vec<u8>) -> String {
    String::from_utf8(v).unwrap()
}

fn read_link(p: *const i8) -> Result<String, ()> {
    let mut buf = Vec::with_capacity(8);
    loop {
        let buf_read = cvt(unsafe { fill(p, buf.as_mut_ptr() as *mut _, buf.capacity()) })? as usize;
        unsafe {
            buf.set_len(buf_read);
        }
        if buf_read != 0 {
            return Ok(from_vec(buf));
        }
    }
}

fn main() {
    let name = b"x\0";
    assert_eq!(read_link(name.as_ptr() as *const i8).unwrap(), "aaaaaaaa");
}
