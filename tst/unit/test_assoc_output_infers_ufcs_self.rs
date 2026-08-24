//@ test-harness

trait Valid {
    fn valid<Alignment>(value: Pointer<ReadOnly<Self>, Alignment>) -> bool
    where
        Self: Sized;
}

impl Valid for u8 {
    fn valid<Alignment>(_: Pointer<ReadOnly<Self>, Alignment>) -> bool {
        true
    }
}

trait HasField<Field, const VARIANT: i128, const FIELD: i128> {
    type Type;
}

trait ProjectField<Field, Invariants, const VARIANT: i128, const FIELD: i128>:
    HasField<Field, VARIANT, FIELD>
{
    type Error;
}

const STRUCT_VARIANT: i128 = -1;
const FIRST_FIELD: i128 = 0;
const SECOND_FIELD: i128 = 1;

impl<A, B> HasField<(), { STRUCT_VARIANT }, { FIRST_FIELD }> for (A, B) {
    type Type = A;
}

impl<A, B> HasField<(), { STRUCT_VARIANT }, { SECOND_FIELD }> for (A, B) {
    type Type = B;
}

struct ReadOnly<T>(T);

impl<T, Field, const VARIANT: i128, const FIELD: i128> HasField<Field, VARIANT, FIELD>
    for ReadOnly<T>
where
    T: HasField<Field, VARIANT, FIELD>,
{
    type Type = ReadOnly<T::Type>;
}

impl<A, B, Invariants> ProjectField<(), Invariants, { STRUCT_VARIANT }, { FIRST_FIELD }>
    for (A, B)
{
    type Error = ();
}

impl<A, B, Invariants> ProjectField<(), Invariants, { STRUCT_VARIANT }, { SECOND_FIELD }>
    for (A, B)
{
    type Error = ();
}

impl<T, Field, Invariants, const VARIANT: i128, const FIELD: i128>
    ProjectField<Field, Invariants, VARIANT, FIELD> for ReadOnly<T>
where
    T: ProjectField<Field, Invariants, VARIANT, FIELD>,
{
    type Error = T::Error;
}

struct Pointer<T, Invariants>(T, Invariants);

impl<T, Invariants> Pointer<T, Invariants> {
    fn project<Field, const VARIANT: i128, const FIELD: i128>(
        self,
    ) -> Result<
        Pointer<<T as HasField<Field, VARIANT, FIELD>>::Type, Invariants>,
        <T as ProjectField<Field, Invariants, VARIANT, FIELD>>::Error,
    >
    where
        T: ProjectField<Field, Invariants, VARIANT, FIELD>,
    {
        loop {}
    }
}

fn into_inner<T, E>(_: Result<T, E>) -> T {
    loop {}
}

fn field_is_valid<T: Valid>(pointer: Pointer<ReadOnly<(u16, T)>, ()>) -> bool {
    Valid::valid(into_inner(
        pointer.project::<_, { STRUCT_VARIANT }, { SECOND_FIELD }>(),
    ))
}

#[test]
fn assoc_output_infers_ufcs_self() {
    let function: fn(Pointer<ReadOnly<(u16, u8)>, ()>) -> bool = field_is_valid::<u8>;
    let _ = function;
}
