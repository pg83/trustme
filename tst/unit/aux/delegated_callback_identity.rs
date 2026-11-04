pub fn any(callback: for<'a> fn(&'a u16) -> u16) -> for<'a> fn(&'a u16) -> u16 {
    callback
}
pub fn fixed(callback: fn(&'static u16) -> u16) -> fn(&'static u16) -> u16 {
    callback
}
