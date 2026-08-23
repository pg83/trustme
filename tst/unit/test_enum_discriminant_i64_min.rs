// A `switch` case for the discriminant `i64::MIN` was emitted as
// `-9223372036854775808ll`, which C++ reads as a negation applied to a positive
// literal too large for `long long`. Written as a subtraction the constant
// stays in range.
//
// Same shape as the upstream test mir/enum/negative_discr_ok.rs.
#[repr(i64)]
#[derive(PartialEq, Eq, PartialOrd, Ord, Debug)]
enum Wide {
    Min = i64::MIN,
    Zero = 0,
    Max = i64::MAX,
}

#[repr(i32)]
#[derive(PartialEq, Debug)]
enum Narrow {
    Min = i32::MIN,
    Zero = 0,
}

fn name(v: &Wide) -> &'static str {
    match v {
        Wide::Min => "min",
        Wide::Zero => "zero",
        Wide::Max => "max",
    }
}

fn main() {
    assert_eq!(name(&Wide::Min), "min");
    assert_eq!(name(&Wide::Zero), "zero");
    assert_eq!(name(&Wide::Max), "max");

    assert_eq!(Wide::Min as i64, i64::MIN);
    assert_eq!(Wide::Max as i64, i64::MAX);

    assert_eq!(Narrow::Min as i32, i32::MIN);
    assert!(matches!(Narrow::Min, Narrow::Min));
    assert!(matches!(Narrow::Zero, Narrow::Zero));

    // The same value round-trips through a comparison.
    assert!(Wide::Min == Wide::Min);
    assert!(Wide::Min != Wide::Max);
    assert!(Wide::Min < Wide::Zero);
    assert_eq!(Wide::Min.cmp(&Wide::Max), std::cmp::Ordering::Less);
}
