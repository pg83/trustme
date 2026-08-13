//@ edition: 2015

fn load<L>() -> Option<L> {
    None
}

fn main() {
    while let Some(tag) = load() {
        match &tag {
            b"NAME" => {}
            b"DATA" => {}
            _ => {}
        }
    }
}
