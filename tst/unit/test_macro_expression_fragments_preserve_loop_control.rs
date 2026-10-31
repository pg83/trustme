macro_rules! keep { ($value:expr) => { $value }; }
fn early_return() -> u8 { keep!(return 23) }

fn main() {
    let mut sum = 0;
    keep!(for value in 1..=4 { sum += value; });
    let result = keep!('pick: loop {
        keep!(for value in 0..4 {
            if value == 0 { keep!(continue); }
            if value == 2 { keep!(break); }
            sum += value;
        });
        keep!(break 'pick sum);
    });
    let mut count = 0;
    keep!(while count < 2 { count += 1; });
    let selected = keep!(match Some(17) { Some(value) => value, None => 0 });
    assert_eq!((result, count, selected, early_return()), (11, 2, 17, 23));
}
