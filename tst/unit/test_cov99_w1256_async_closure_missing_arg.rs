//@ compile-fail: type annotations needed
//@ compile-flags: --emit=metadata -Cpanic=abort

fn main() {
    let call = async |value: u8| value;
    let _future = call();
}
