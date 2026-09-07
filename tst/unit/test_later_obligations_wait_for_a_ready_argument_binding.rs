/* rustc checks a body in order: `Sections::load(section)` coerces `section: F` into
   the callee's `?F` at once, and the callee's bound `F: FnMut(SectionId) -> Result<R,
   ?E>` then fixes `?E = E` before the `?` is even lowered.  With every rule present at
   once, the `?`'s `E: From<?E>` came first and chose the where-clause `E: From<Error>`,
   so `?E = Error` and `F`'s bound could not be met.  A rule registered after a ready
   argument binding now waits for that binding's pass. */
struct Error;
struct SectionId;
struct Sections<R>(R);

impl<R> Sections<R> {
    fn load<F, E>(mut section: F) -> Result<Self, E>
    where
        F: FnMut(SectionId) -> Result<R, E>,
        E: From<Error>,
    {
        Ok(Sections(section(SectionId)?))
    }
}

struct Package<R>(Sections<R>);

impl<R> Package<R> {
    fn load<F, E>(section: F) -> Result<Self, E>
    where
        F: FnMut(SectionId) -> Result<R, E>,
        E: From<Error>,
    {
        let sections = Sections::load(section)?;
        Ok(Package(sections))
    }
}

fn main() {
    let p: Result<Package<u8>, Error> = Package::load(|_| Ok(1u8));
    assert!(p.is_ok());
}
