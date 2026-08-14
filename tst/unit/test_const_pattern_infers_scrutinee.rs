#[derive(Copy, Clone)]
struct NotStructural(u32);

impl PartialEq for NotStructural {
    fn eq(&self, _: &Self) -> bool {
        false
    }
}

impl Eq for NotStructural {}

type Optional = Option<NotStructural>;
const NONE: Optional = None;

fn main() {
    match None {
        NONE => {}
        _ => panic!("const pattern did not match"),
    }
}
