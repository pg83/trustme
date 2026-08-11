// { dg-additional-options "-w" }

#[path = "issue-1089/test_mod.rs"]
pub mod test_mod;

fn main() {
    let a = test_mod::Test(123);
}
