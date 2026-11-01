const YES: bool = true;
const NO: bool = false;

fn main() {
    let and = (YES & YES, YES & NO, NO & NO);
    let or = (YES | YES, YES | NO, NO | NO);
    let xor = (YES ^ YES, YES ^ NO, NO ^ NO);
    assert_eq!(and, (true, false, false));
    assert_eq!(or, (true, true, false));
    assert_eq!(xor, (false, true, false));
}
