type Transform = Box<dyn Fn(&[u8], &mut [u8], &Info) + Send + Sync>;

struct Info;

fn first(_: &[u8], output: &mut [u8], _: &Info) {
    output[0] = 1;
}

fn second(_: &[u8], output: &mut [u8], _: &Info) {
    output[0] = 2;
}

fn transform(first_selected: bool) -> Result<Transform, ()> {
    Ok(Box::new(if first_selected { first } else { second }))
}

fn main() {
    let mut output = [0];
    transform(true).unwrap()(&[], &mut output, &Info);
    assert_eq!(output[0], 1);
}
