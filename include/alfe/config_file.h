#include "alfe/main.h"

#ifndef INCLUDED_CONFIG_FILE_H
#define INCLUDED_CONFIG_FILE_H

#include "alfe/hash_table.h"
#include "alfe/space.h"
#include "alfe/any.h"
#include "alfe/type.h"
#include "alfe/value.h"
#include "alfe/set.h"

int parseHexadecimalCharacter(CharacterSource* source, Span* span)
{
    CharacterSource s = *source;
    int c = s.get(span);
    if (c >= '0' && c <= '9') {
        *source = s;
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        *source = s;
        return c + 10 - 'A';
    }
    if (c >= 'a' && c <= 'f') {
        *source = s;
        return c + 10 - 'a';
    }
    return -1;
}

class ConfigFile
{
public:
    ConfigFile()
      : _arrayConversionSource(new ArrayConversionSourceImplementation)
    {
        _typeConverter.addConversionSource(Template::array,
            _arrayConversionSource);
    }
    void addType(Type type)
    {
        if (_typeSet.has(type))
            return;
        _typeSet.add(type);
        String name = type.toString();
        _types.add(name, type);
        EnumerationType t(type);
        if (t.valid()) {
            const Array<EnumerationType::Value>* values = t.values();
            for (int i = 0; i < values->count(); ++i) {
                EnumerationType::Value value = (*values)[i];
                _enumeratedValues.add(value.name(),
                    TypedValue(type, value.value()));
            }
            return;
        }
        StructuredType s(type);
        if (s.valid()) {
            const Array<StructuredType::Member>* members = s.members();
            for (int i = 0; i < members->count(); ++i)
                addType((*members)[i].type());
        }
    }
    void addOption(String name, Type type)
    {
        _options.add(name, TypedValue(type));
    }
    template<class T> void addOption(String name, Type type, T defaultValue)
    {
        _options.add(name, TypedValue(type, Any(defaultValue)));
    }
    void addConversion(const Type& from, const Type& to,
        const Conversion& conversion)
    {
        _typeConverter.addConversion(from, to, conversion);
    }
    TypedValue convert(TypedValue e, Type expectedType)
    {
        return _typeConverter.conversion(e.type(), expectedType)(e);
    }

    void load(File file)
    {
        String contents = file.contents();
        CharacterSource source(contents, file.path());
        Space::parse(&source);
        do {
            CharacterSource s = source;
            if (s.get() == -1)
                break;
            Identifier identifier = parseIdentifier(&source);
            String name = identifier.name();
            Span span;
            if (name == "include") {
                TypedValue e = convert(parseExpression(&source), Type::string);
                Space::assertCharacter(&source, ';', &span);
                load(e.value<String>());
            }
            if (!_options.hasKey(name))
                identifier.span().throwError("Unknown identifier " + name);
            Space::assertCharacter(&source, '=', &span);
            TypedValue e = convert(parseExpression(&source),
                _options[name].type());
            Space::assertCharacter(&source, ';', &span);
            _options[name].setValue(e.value());
        } while (true);
        for (HashTable<String, TypedValue>::Iterator i = _options.begin();
            i != _options.end(); ++i) {
            if (!i.value().valid())
                throw Exception(file.path() + ": " + i.key() +
                    " not defined and no default is available.");
        }
    }
    template<class T> T get(String name) { return get(name).value<T>(); }
    TypedValue get(String name) { return _options[name]; }
private:
    class Identifier
    {
    public:
        Identifier() { }
        Identifier(String name, Span span) : _name(name), _span(span) { }
        String name() const { return _name; }
        Span span() const { return _span; }
        bool valid() const { return !_name.empty(); }
    private:
        String _name;
        Span _span;
    };

    Identifier parseIdentifier(CharacterSource* source)
    {
        CharacterSource s = *source;
        Location startLocation = s.location();
        int startOffset = s.offset();
        Span startSpan;
        Span endSpan;
        int c = s.get(&startSpan);
        if (!(c >= 'A'&& c <= 'Z') && !(c >= 'a' && c <= 'z'))
            return Identifier();
        CharacterSource s2;
        do {
            s2 = s;
            c = s.get(&endSpan);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_')
                continue;
            break;
        } while (true);
        int endOffset = s2.offset();
        Location endLocation = s2.location();
        Space::parse(&s2);
        String name = s2.subString(startOffset, endOffset);
        *source = s2;
        return Identifier(name, startSpan + endSpan);
    }
    void throwError(CharacterSource* source)
    {
        source->location().throwError("Expected expression");
    }

    TypedValue combine(TypedValue left, TypedValue right)
    {
        if (left.valid())
            return TypedValue(Type::string,
                left.value<String>() + right.value<String>(),
                left.span() + right.span());
        return right;
    }

    TypedValue parseDoubleQuotedString(CharacterSource* source)
    {
        Span span;
        Span startSpan;
        if (!source->parse('"', &startSpan))
            return TypedValue();
        Span stringStartSpan = startSpan;
        Span stringEndSpan = startSpan;
        int startOffset = source->offset();
        int endOffset;
        String insert;
        int n;
        int nn;
        String string;
        TypedValue expression;
        TypedValue part;
        do {
            CharacterSource s = *source;
            endOffset = s.offset();
            int c = s.get(&span);
            if (c < 0x20 && c != 10) {
                if (c == -1)
                    source->location().throwError("End of file in string");
                source->throwUnexpected("printable character");
            }
            *source = s;
            switch (c) {
                case '"':
                    string += s.subString(startOffset, endOffset);
                    Space::parse(source);
                    return combine(expression, TypedValue(Type::string, string,
                        stringStartSpan + span));
                case '\\':
                    string += s.subString(startOffset, endOffset);
                    c = s.get(&stringEndSpan);
                    if (c < 0x20) {
                        if (c == 10)
                            source->location().throwError(
                                "End of line in string");
                        if (c == -1)
                            source->location().throwError(
                                "End of file in string");
                        source->throwUnexpected("escaped character");
                    }
                    *source = s;
                    switch (c) {
                        case 'n':
                            insert = "\n";
                            break;
                        case 't':
                            insert = codePoint(9);
                            break;
                        case '$':
                            insert = "$";
                            break;
                        case '"':
                            insert = "\"";
                            break;
                        case '\'':
                            insert = "'";
                            break;
                        case '`':
                            insert = "`";
                            break;
                        case 'U':
                            source->assert('+', &stringEndSpan);
                            n = 0;
                            for (int i = 0; i < 4; ++i) {
                                nn = parseHexadecimalCharacter(source,
                                    &stringEndSpan);
                                if (nn == -1)
                                    source->
                                        throwUnexpected("hexadecimal digit");
                                n = (n << 4) | nn;
                            }
                            nn = parseHexadecimalCharacter(source,
                                &stringEndSpan);
                            if (nn != -1) {
                                n = (n << 4) | nn;
                                nn = parseHexadecimalCharacter(source,
                                    &stringEndSpan);
                                if (nn != -1)
                                    n = (n << 4) | nn;
                            }
                            insert = codePoint(n);
                            break;
                        default:
                            source->throwUnexpected("escaped character");
                    }
                    string += insert;
                    startOffset = source->offset();
                    break;
                case '$':
                    {
                        Identifier i = parseIdentifier(source);
                        if (!i.valid()) {
                            if (Space::parseCharacter(source, '(', &span)) {
                                part = parseExpression(source);
                                source->assert(')', &span);
                            }
                            else
                                source->location().throwError("Expected "
                                    "identifier or parenthesized expression");
                        }
                        String s = i.name();
                        if (s[0] >= 'a' && s[0] <= 'z')
                            part = valueOfIdentifier(i);
                        else
                            i.span().throwError("Expected identifier or "
                            "parenthesized expression");
                    }
                    if (part.type() == Type::integer)
                        part = TypedValue(Type::string,
                            decimal(part.value<int>()), part.span());
                    else
                        if (part.type() != Type::string)
                            source->location().throwError(
                                "Don't know how to convert type " +
                                part.type().toString() + " to a string");
                    string += s.subString(startOffset, endOffset);
                    startOffset = source->offset();
                    expression = combine(expression, TypedValue(Type::string,
                        string, stringStartSpan + stringEndSpan));
                    string = "";
                    expression = combine(expression, part);
                    break;
                default:
                    stringEndSpan = span;
            }
        } while (true);
    }

    TypedValue parseEmbeddedLiteral(CharacterSource* source)
    {
        Span startSpan;
        Span endSpan;
        if (!source->parse('#', &startSpan))
            return TypedValue();
        if (!source->parse('#', &endSpan))
            return TypedValue();
        if (!source->parse('#', &endSpan))
            return TypedValue();
        int startOffset = source->offset();
        Location location = source->location();
        CharacterSource s = *source;
        do {
            int c = s.get();
            if (c == -1)
                source->location().throwError("End of file in string");
            if (c == 10)
                break;
            *source = s;
        } while (true);
        int endOffset = source->offset();
        String terminator = source->subString(startOffset, endOffset);
        startOffset = s.offset();
        CharacterSource terminatorSource(terminator);
        int cc = terminatorSource.get();
        String string;
        do {
            *source = s;
            int c = s.get();
            if (c == -1)
                source->location().throwError("End of file in string");
            if (cc == -1) {
                if (c != '#')
                    continue;
                CharacterSource s2 = s;
                if (s2.get() != '#')
                    continue;
                if (s2.get(&endSpan) != '#')
                    continue;
                string += s.subString(startOffset, source->offset());
                *source = s2;
                Space::parse(source);
                return TypedValue(Type::string, string, startSpan + endSpan);
            }
            else
                if (c == cc) {
                    CharacterSource s2 = s;
                    CharacterSource st = terminatorSource;
                    do {
                        int ct = st.get();
                        if (ct == -1) {
                            if (s2.get() != '#')
                                break;
                            if (s2.get() != '#')
                                break;
                            if (s2.get(&endSpan) != '#')
                                break;
                            string +=
                                s.subString(startOffset, source->offset());
                            *source = s2;
                            Space::parse(source);
                            return TypedValue(Type::string, string,
                                startSpan + endSpan);
                        }
                        int cs = s2.get();
                        if (ct != cs)
                            break;
                    } while (true);
                }
            if (c == 10) {
                string += s.subString(startOffset, source->offset()) +
                    codePoint(10);
                startOffset = s.offset();
            }
        } while (true);
    }

    TypedValue parseInteger(CharacterSource* source)
    {
        CharacterSource s = *source;
        int n = 0;
        Span span;
        int c = s.get(&span);
        if (c < '0' || c > '9')
            return TypedValue();
        if (c == '0') {
            CharacterSource s2 = s;
            Span span2;
            int c = s2.get(&span2);
            if (c == 'x') {
                bool okay = false;
                int n = 0;
                do {
                    c = s2.get(&span2);
                    if (c >= '0' && c <= '9')
                        n = n*0x10 + c - '0';
                    else
                        if (c >= 'A' && c <= 'F')
                            n = n*0x10 + c + 10 - 'A';
                        else
                            if (c >= 'a' && c <= 'f')
                                n = n*0x10 + c + 10 - 'a';
                            else
                                if (okay) {
                                    Space::parse(source);
                                    return TypedValue(Type::integer, n, span);
                                }
                                else
                                    return TypedValue();
                    okay = true;
                    *source = s2;
                    span += span2;
                } while (true);
            }
        }
        do {
            n = n*10 + c - '0';
            *source = s;
            Span span2;
            c = s.get(&span2);
            if (c < '0' || c > '9') {
                Space::parse(source);
                return TypedValue(Type::integer, n, span);
            }
            span += span2;
        } while (true);
    }
    TypedValue valueOfIdentifier(const Identifier& i)
    {
        String s = i.name();
        static String trueKeyword("true");
        if (s == trueKeyword)
            return TypedValue(Type::boolean, true, i.span());
        static String falseKeyword("false");
        if (s == falseKeyword)
            return TypedValue(Type::boolean, false, i.span());
        if (_enumeratedValues.hasKey(s)) {
            TypedValue value = _enumeratedValues[s];
            return TypedValue(value.type(), value.value(), i.span());
        }
        if (!_options.hasKey(s))
            i.span().throwError("Unknown identifier " + s);
        TypedValue option = _options[s];
        return TypedValue(option.type(), option.value(), i.span());
    }
    TypedValue parseStructuredExpression(CharacterSource* source,
        List<TypedValue> values, TypedValue label, Span span)
    {
        Value<HashTable<String, TypedValue> > table;
        List<StructuredType::Member> members;
        int n = 0;
        for (auto i = values.begin(); i != values.end(); ++i) {
            String name(decimal(n));
            table->add(name, *i);
            members.add(StructuredType::Member("", (*i).type()));
            ++n;
        }
        TypedValue v = parseExpression(source);
        if (!v.valid())
            source->location().throwError("Expected expression");
        String name = label.value<String>();
        table->add(name, v);
        members.add(StructuredType::Member(name, v.type()));
        Span span2;
        while (Space::parseCharacter(source, ',', &span2)) {
            Identifier identifier = parseIdentifier(source);
            if (!identifier.valid())
                source->location().throwError("Expected label");
            Space::assertCharacter(source, ':', &span2);
            TypedValue value = parseExpression(source);
            String name = identifier.name();
            if (table->hasKey(name))
                identifier.span().throwError(name + " already defined.");
            table->add(name, value);
            members.add(StructuredType::Member(name, value.type()));
        }
        Space::assertCharacter(source, '}', &span2);
        return TypedValue(StructuredType("", members), table, span + span2);
    }
    TypedValue parseExpressionElement(CharacterSource* source)
    {
        Location location = source->location();
        TypedValue e = parseDoubleQuotedString(source);
        if (e.valid())
            return e;
        e = parseEmbeddedLiteral(source);
        if (e.valid())
            return e;
        e = parseInteger(source);
        if (e.valid())
            return e;
        Identifier i = parseIdentifier(source);
        if (i.valid()) {
            String name = i.name();
            if (name[0] >= 'a' && name[0] <= 'z') {
                Span span;
                if (Space::parseCharacter(source, ':', &span)) {
                    // Avoid interpreting labels as values
                    return TypedValue(Type::label, name, i.span() + span);
                }
                return valueOfIdentifier(i);
            }
            // i is a type identifier
            if (!_types.hasKey(name))
                i.span().throwError("Unknown type " + name);
            StructuredType type = _types[name];
            if (!type.valid())
                i.span().throwError("Only structure types can be constructed");
            const Array<StructuredType::Member>* members = type.members();
            List<Any> values;
            Span span;
            Space::assertCharacter(source, '(', &span);
            for (int i = 0; i < members->count(); ++i) {
                if (i > 0)
                    Space::assertCharacter(source, ',', &span);
                TypedValue value = convert(parseExpression(source),
                    (*members)[i].type());
                values.add(value.value());
            }
            Space::assertCharacter(source, ')', &span);
            return TypedValue(type, values, e.span() + span);
        }
        Span span;
        if (Space::parseCharacter(source, '{', &span)) {
            List<TypedValue> values;
            Span span2;
            List<Type> types;
            do {
                TypedValue e = parseExpression(source);
                if (e.type() == Type::label)
                    return parseStructuredExpression(source, values, e, span);
                values.add(e);
                types.add(e.type());
            } while (Space::parseCharacter(source, ',', &span2));
            Space::assertCharacter(source, '}', &span2);
            return TypedValue(Type::tuple(types), values, span + span2);
        }
        if (Space::parseCharacter(source, '(', &span)) {
            e = parseExpression(source);
            Space::assertCharacter(source, ')', &span);
            return e;
        }
        return TypedValue();
    }

    TypedValue parseUnaryExpression(CharacterSource* source)
    {
        Span span;
        if (Space::parseCharacter(source, '-', &span)) {
            TypedValue e = parseUnaryExpression(source);
            span = span + e.span();
            if (e.type() != Type::integer)
                span.throwError("Only numbers can be negated");
            return TypedValue(Type::integer, -e.value<int>(), span);
        }
        return parseExpressionElement(source);
    }

    TypedValue parseMultiplicativeExpression(CharacterSource* source)
    {
        TypedValue e = parseUnaryExpression(source);
        if (!e.valid())
            return e;
        do {
            Span span;
            if (Space::parseCharacter(source, '*', &span)) {
                TypedValue e2 = parseUnaryExpression(source);
                if (!e2.valid())
                    throwError(source);
                bool okay = false;
                if (e.type() == Type::integer) {
                    if (e2.type() == Type::integer) {
                        e = TypedValue(Type::integer,
                            e.value<int>() * e2.value<int>(),
                            e.span() + e2.span());
                        okay = true;
                    }
                    else
                        if (e2.type() == Type::string) {
                            e = TypedValue(Type::string,
                                e2.value<String>() * e.value<int>(),
                                e.span() + e2.span());
                            okay = true;
                        }
                }
                else
                    if (e.type() == Type::string) {
                        if (e2.type() == Type::integer) {
                            e = TypedValue(Type::string,
                                e.value<String>() * e2.value<int>(),
                                e.span() + e2.span());
                            okay = true;
                        }
                    }
                if (!okay)
                    span.throwError("Don't know how to multiply type " +
                        e.type().toString() + " and type " +
                        e2.type().toString() + ".");
                continue;
            }
            if (Space::parseCharacter(source, '/', &span)) {
                TypedValue e2 = parseUnaryExpression(source);
                if (!e2.valid())
                    throwError(source);
                if (e.type() == Type::integer && e2.type() == Type::integer)
                    e = TypedValue(Type::integer,
                        e.value<int>() / e2.value<int>(),
                        e.span() + e2.span());
                else
                    span.throwError("Don't know how to divide type " +
                        e.type().toString() + " by type " +
                        e2.type().toString() + ".");
                continue;
            }
            return e;
        } while (true);
    }

    TypedValue parseExpression(CharacterSource* source)
    {
        TypedValue e = parseMultiplicativeExpression(source);
        if (!e.valid())
            throwError(source);
        do {
            Span span;
            if (Space::parseCharacter(source, '+', &span)) {
                TypedValue e2 = parseMultiplicativeExpression(source);
                if (!e2.valid())
                    throwError(source);
                if (e.type() == Type::integer && e2.type() == Type::integer)
                    e = TypedValue(Type::integer,
                        e.value<int>() + e2.value<int>(),
                        e.span() + e2.span());
                else
                    if (e.type() == Type::string && e2.type() == Type::string)
                        e = TypedValue(Type::string,
                            e.value<String>() + e2.value<String>(),
                            e.span() + e2.span());
                    else
                        span.throwError("Don't know how to add type " +
                            e2.type().toString() + " to type " +
                            e.type().toString() + ".");
                continue;
            }
            if (Space::parseCharacter(source, '-', &span)) {
                TypedValue e2 = parseMultiplicativeExpression(source);
                if (!e2.valid())
                    throwError(source);
                if (e.type() == Type::integer && e2.type() == Type::integer)
                    e = TypedValue(Type::integer,
                        e.value<int>() - e2.value<int>(),
                        e.span() + e2.span());
                else
                    span.throwError("Don't know how to subtract type " +
                        e2.type().toString() + " from type " +
                        e.type().toString() + ".");
                continue;
            }
            return e;
        } while (true);
    }

    HashTable<String, TypedValue> _options;
    HashTable<String, TypedValue> _enumeratedValues;
    HashTable<String, Type> _types;
    Set<Type> _typeSet;
    TypeConverter _typeConverter;
    ConversionSource _arrayConversionSource;
};

#endif // INCLUDED_CONFIG_FILE_H
