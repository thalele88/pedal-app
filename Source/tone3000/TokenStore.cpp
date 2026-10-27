// System headers before JUCE: MacTypes' Point would clash with juce::Point.
#if defined(__APPLE__)
 #include <Security/Security.h>
#elif defined(_WIN32)
 #include <windows.h>
 #include <wincrypt.h>
#endif

#include "tone3000/TokenStore.h"

namespace pedal::tone3000
{

TokenStore::TokenStore (juce::String s, juce::String a) : service (std::move (s)), account (std::move (a)) {}

#if JUCE_MAC || JUCE_IOS

namespace
{
struct CFReleaser
{
    void operator() (CFTypeRef ref) const
    {
        if (ref != nullptr)
            CFRelease (ref);
    }
};
template <typename T>
using CFPtr = std::unique_ptr<std::remove_pointer_t<T>, CFReleaser>;

CFPtr<CFStringRef> cfString (const juce::String& s)
{
    return CFPtr<CFStringRef> (CFStringCreateWithCString (nullptr, s.toRawUTF8(), kCFStringEncodingUTF8));
}

CFPtr<CFMutableDictionaryRef> baseQuery (const juce::String& service, const juce::String& account)
{
    CFPtr<CFMutableDictionaryRef> q (CFDictionaryCreateMutable (nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                &kCFTypeDictionaryValueCallBacks));
    auto svc = cfString (service);
    auto acc = cfString (account);
    CFDictionarySetValue (q.get(), kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue (q.get(), kSecAttrService, svc.get());
    CFDictionarySetValue (q.get(), kSecAttrAccount, acc.get());
    return q;
}
} // namespace

bool TokenStore::save (const juce::String& secret)
{
    CFPtr<CFDataRef> data (CFDataCreate (nullptr, (const UInt8*) secret.toRawUTF8(), (CFIndex) secret.getNumBytesAsUTF8()));

    auto query = baseQuery (service, account);
    CFPtr<CFMutableDictionaryRef> update (CFDictionaryCreateMutable (nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                     &kCFTypeDictionaryValueCallBacks));
    CFDictionarySetValue (update.get(), kSecValueData, data.get());

    auto status = SecItemUpdate (query.get(), update.get());
    if (status == errSecItemNotFound)
    {
        CFDictionarySetValue (query.get(), kSecValueData, data.get());
        CFDictionarySetValue (query.get(), kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);
        status = SecItemAdd (query.get(), nullptr);
    }
    return status == errSecSuccess;
}

juce::String TokenStore::load() const
{
    auto query = baseQuery (service, account);
    CFDictionarySetValue (query.get(), kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue (query.get(), kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef result = nullptr;
    if (SecItemCopyMatching (query.get(), &result) != errSecSuccess || result == nullptr)
        return {};

    CFPtr<CFDataRef> data ((CFDataRef) result);
    return juce::String::fromUTF8 ((const char*) CFDataGetBytePtr (data.get()), (int) CFDataGetLength (data.get()));
}

void TokenStore::remove()
{
    auto query = baseQuery (service, account);
    SecItemDelete (query.get());
}

#else

namespace
{
juce::File storageFile (const juce::String& service, const juce::String& account)
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (service)
        .getChildFile (account + ".token");
}
} // namespace

 #if JUCE_WINDOWS

bool TokenStore::save (const juce::String& secret)
{
    DATA_BLOB in { (DWORD) secret.getNumBytesAsUTF8(), (BYTE*) secret.toRawUTF8() };
    DATA_BLOB out {};
    if (! CryptProtectData (&in, L"Pedal App TONE3000", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;

    const auto encoded = juce::Base64::toBase64 (out.pbData, out.cbData);
    LocalFree (out.pbData);

    const auto file = storageFile (service, account);
    return file.getParentDirectory().createDirectory() && file.replaceWithText (encoded);
}

juce::String TokenStore::load() const
{
    const auto encoded = storageFile (service, account).loadFileAsString();
    if (encoded.isEmpty())
        return {};

    juce::MemoryOutputStream raw;
    if (! juce::Base64::convertFromBase64 (raw, encoded))
        return {};

    DATA_BLOB in { (DWORD) raw.getDataSize(), (BYTE*) raw.getData() };
    DATA_BLOB out {};
    if (! CryptUnprotectData (&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};

    auto secret = juce::String::fromUTF8 ((const char*) out.pbData, (int) out.cbData);
    LocalFree (out.pbData);
    return secret;
}

 #else

// Linux is only built by CI; good enough for a developer machine.
bool TokenStore::save (const juce::String& secret)
{
    const auto file = storageFile (service, account);
    return file.getParentDirectory().createDirectory() && file.replaceWithText (secret);
}

juce::String TokenStore::load() const
{
    return storageFile (service, account).loadFileAsString();
}

 #endif

void TokenStore::remove()
{
    storageFile (service, account).deleteFile();
}

#endif

} // namespace pedal::tone3000
