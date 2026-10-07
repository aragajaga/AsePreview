# C++ formatting

- Write application messages, code comments, and project documentation in English. Preserve user-provided names and other data in their original language.

- Prioritize readability and clear vertical structure over minimizing line count. Do not compress code just because it fits on one line.
- Use 4 spaces for indentation. Do not use tabs for indentation or `using namespace`.
- Align `public:`, `protected:`, and `private:` with their class declaration, including nested classes. Indent class members by 4 spaces relative to that declaration.
- Indent continuation lines in increments of 4 spaces relative to the current nesting level. Do not align them with arguments or operators in the middle of a line.
- Put each independent statement or expression on its own line. Never combine multiple statements on one line.
- Place opening braces for functions, conditions, and loops on the header line. Start `else` and `catch` on a new line after the closing brace. Use `} while (...);` for `do`/`while`.
- Use braces around condition and loop bodies. Do not collapse short constructs, blocks, or lambdas onto one line. One-line functions are allowed only where there is a strong existing local convention; automatic formatting expands them.
- Prefer readable multiline expressions. Do not mechanically wrap or merge code when this makes its structure less clear.
- When editing existing code, respect the surrounding formatting and do not reformat unrelated sections.
- Use the root `.clang-format`. Check the prohibition on `using namespace` manually: `clang-format` does not enforce it.

Example:

```cpp
bool Process(const Path& path) {
    auto image = LoadImage(path);

    if (!image) {
        return false;
    }
    else {
        ProcessImage(image);
    }

    try {
        SaveImage(image);
    }
    catch (...) {
        return false;
    }

    return true;
}
```

## Logical visual structure

- When writing or modifying C++ code, first identify the logical operations within each brace scope, then format them as visual sections.
- Keep related statements for one operation together. Separate distinct sections with a blank line: for example, data acquisition, validation, resource loading, processing, and saving.
- Do not turn a scope into a dense, uninterrupted sequence of statements or combine different logical operations into one visual block.
- Preserve meaningful blank lines even if the code remains understandable without them. Do not remove them to save vertical space.
- Make the structure of a scope apparent at a glance before reading every statement: use clear grouping, vertical readability, and an obvious visual hierarchy.
- `clang-format` cannot identify semantic sections or insert their separators. Organizing sections and checking their formatting are the responsibility of the author and code review.

Example of separate stages:

```cpp
void ProcessFile(const Path& path) {
    File file = OpenFile(path);
    Metadata metadata = ReadMetadata(file);

    if (!ValidateMetadata(metadata)) {
        return;
    }

    Image image = LoadImage(file);

    ProcessImage(image);
    ApplyEffects(image);

    SaveImage(image);
}
```

### Section comments

When a section needs explanation, place a concise comment immediately before it. The comment applies to the entire following section and explains its purpose, role, or intent. Do not comment on every small block or narrate the obvious actions of individual statements.

Preferred:

```cpp
// Prepare metadata for subsequent image processing.
Metadata metadata = ReadMetadata(file);
UpdateMetadataCache(metadata);
ValidateMetadata(metadata);
```

Avoid:

```cpp
Metadata metadata = ReadMetadata(file);
// Update cache.
UpdateMetadataCache(metadata);
// Validate metadata.
ValidateMetadata(metadata);
```
