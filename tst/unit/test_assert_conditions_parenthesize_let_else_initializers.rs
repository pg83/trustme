macro_rules! optional {
    () => { Some(809u16) };
}

fn main() {
    assert!({
        let Some(value) = ({ Some(743u16) }) else { panic!("missing block value") };
        value == 743
    });
    assert!({
        let Some(value) = optional! {} else { panic!("missing macro value") };
        value == 809
    });
}
