enum Value {
    Present,
}

type Alias = Value;

macro_rules! is_match {
    ($pattern:pat) => {
        match Alias::Present {
            $pattern => true,
            _ => false,
        }
    };
}

fn main() {
    assert!(is_match!(<Alias>::Present));
}
