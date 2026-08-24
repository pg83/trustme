use self::Normalization::*;

macro_rules! normalizations {
    ($($name:ident,)*) => {
        #[derive(PartialOrd, PartialEq, Copy, Clone, Debug)]
        enum Normalization {
            $($name,)*
        }

        impl Normalization {
            const ALL: &'static [Self] = &[$($name),*];
        }

        impl Default for Variations {
            fn default() -> Self {
                Variations {
                    variations: [$(($name, String::new()).1),*],
                }
            }
        }
    };
}

normalizations! {
    Basic,
    StripCouldNotCompile,
    StripCouldNotCompile2,
    StripForMoreInformation,
    StripForMoreInformation2,
    TrimEnd,
    RustLib,
    TypeDirBackslash,
    WorkspaceLines,
    PathDependencies,
    CargoRegistry,
    ArrowOtherCrate,
    RelativeToDir,
    LinesOutsideInputFile,
    Unindent,
    AndOthers,
    StripLongTypeNameFiles,
    UnindentAfterHelp,
    AndOthersVerbose,
    UnindentMultilineNote,
    DependencyVersion,
    HeadingNote,
    UnindentSuggestion,
    CustomRegistry,
    MorePathDependencies,
}

struct Variations {
    variations: [String; Normalization::ALL.len()],
}

fn main() {
    assert_eq!(Variations::default().variations.len(), 25);
}
