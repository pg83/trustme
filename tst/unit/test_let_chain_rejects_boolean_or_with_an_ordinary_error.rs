//@ compile-fail: error:0: boolean or is not allowed in let chains

fn main() {
    if let Some(value) = Some(769u16) || false {
        let _ = value;
    }
}
