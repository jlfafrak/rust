#include <stdint.h>
#include <string.h>

static int32_t eos_addrinfo_hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int32_t eos_addrinfo_parse_ipv4(const char *source,
                                       uint8_t destination[4]) {
    uint32_t part;
    uint32_t index;
    const char *cursor = source;
    if (source == NULL || *source == '\0') return 0;
    for (index = 0; index < 4; ++index) {
        const char *start = cursor;
        part = 0;
        if (*cursor < '0' || *cursor > '9') return 0;
        while (*cursor >= '0' && *cursor <= '9') {
            part = part * 10U + (uint32_t)(*cursor - '0');
            if (part > 255U) return 0;
            ++cursor;
        }
        if (cursor - start > 1 && *start == '0') return 0;
        destination[index] = (uint8_t)part;
        if (index == 3) return *cursor == '\0';
        if (*cursor != '.') return 0;
        ++cursor;
    }
    return 0;
}

static int32_t eos_addrinfo_parse_ipv6(const char *source,
                                       uint8_t destination[16]) {
    uint8_t temporary[16];
    uint8_t *cursor = temporary;
    uint8_t *end = temporary + sizeof(temporary);
    uint8_t *compression = NULL;
    const char *token;
    const char *text = source;
    uint32_t value = 0;
    uint32_t digits = 0;
    int32_t digit;
    if (source == NULL || *source == '\0') return 0;
    (void)memset(temporary, 0, sizeof(temporary));
    if (*text == ':') {
        ++text;
        if (*text != ':') return 0;
    }
    token = text;
    while (*text != '\0') {
        digit = eos_addrinfo_hex_digit(*text);
        if (digit >= 0) {
            if (++digits > 4) return 0;
            value = (value << 4U) | (uint32_t)digit;
            ++text;
            continue;
        }
        if (*text == ':') {
            token = ++text;
            if (digits == 0) {
                if (compression != NULL) return 0;
                compression = cursor;
                continue;
            }
            if (cursor + 2 > end) return 0;
            *cursor++ = (uint8_t)(value >> 8U);
            *cursor++ = (uint8_t)value;
            digits = 0;
            value = 0;
            if (*text == '\0') return 0;
            continue;
        }
        if (*text == '.' && cursor + 4 <= end &&
            eos_addrinfo_parse_ipv4(token, cursor)) {
            cursor += 4;
            digits = 0;
            while (*text != '\0') ++text;
            break;
        }
        return 0;
    }
    if (digits != 0) {
        if (cursor + 2 > end) return 0;
        *cursor++ = (uint8_t)(value >> 8U);
        *cursor++ = (uint8_t)value;
    }
    if (compression != NULL) {
        size_t tail = (size_t)(cursor - compression);
        if (cursor == end) return 0;
        (void)memmove(end - tail, compression, tail);
        (void)memset(compression, 0, (size_t)((end - tail) - compression));
        cursor = end;
    }
    if (cursor != end) return 0;
    (void)memcpy(destination, temporary, sizeof(temporary));
    return 1;
}

static char *eos_addrinfo_append_decimal(char *cursor, uint32_t value) {
    char reversed[10];
    uint32_t count = 0;
    do {
        reversed[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0);
    while (count != 0) *cursor++ = reversed[--count];
    return cursor;
}

static char *eos_addrinfo_append_hex(char *cursor, uint16_t value) {
    static const char digits[] = "0123456789abcdef";
    uint32_t shift = 12;
    uint32_t started = 0;
    while (shift != 0) {
        uint32_t digit = ((uint32_t)value >> shift) & 15U;
        if (digit != 0 || started != 0) {
            *cursor++ = digits[digit];
            started = 1;
        }
        shift -= 4;
    }
    *cursor++ = digits[value & 15U];
    return cursor;
}

static uint32_t eos_addrinfo_format_ipv4(const uint8_t source[4],
                                         char destination[16]) {
    char *cursor = destination;
    uint32_t index;
    for (index = 0; index < 4; ++index) {
        if (index != 0) *cursor++ = '.';
        cursor = eos_addrinfo_append_decimal(cursor, source[index]);
    }
    *cursor = '\0';
    return (uint32_t)(cursor - destination);
}

static uint32_t eos_addrinfo_format_ipv6(const uint8_t source[16],
                                         char destination[40]) {
    uint16_t words[8];
    uint32_t best_start = 8;
    uint32_t best_length = 0;
    uint32_t index;
    char *cursor = destination;
    for (index = 0; index < 8; ++index) {
        uint32_t run;
        words[index] = (uint16_t)(((uint16_t)source[index * 2U] << 8U) |
                                  source[index * 2U + 1U]);
        if (words[index] != 0) continue;
        run = index;
        while (run < 8 &&
               source[run * 2U] == 0 && source[run * 2U + 1U] == 0) {
            ++run;
        }
        if (run - index > best_length) {
            best_start = index;
            best_length = run - index;
        }
        index = run - 1U;
    }
    if (best_length < 2) best_start = 8;
    for (index = 0; index < 8;) {
        if (index == best_start) {
            *cursor++ = ':';
            *cursor++ = ':';
            index += best_length;
            continue;
        }
        if (cursor != destination && cursor[-1] != ':') *cursor++ = ':';
        cursor = eos_addrinfo_append_hex(cursor, words[index]);
        ++index;
    }
    *cursor = '\0';
    return (uint32_t)(cursor - destination);
}

int32_t eos_rust_inet_pton(int32_t family, const char *source,
                           void *destination) {
    uint8_t bytes[16];
    int32_t result;
    if (source == NULL || destination == NULL) {
        return eos_socket_fail(EOS_ERRNO_FAULT);
    }
    if (family == EOS_RUST_AF_INET) {
        result = eos_addrinfo_parse_ipv4(source, bytes);
        if (result != 0) (void)memcpy(destination, bytes, 4);
        return result;
    }
    if (family == EOS_RUST_AF_INET6) {
        result = eos_addrinfo_parse_ipv6(source, bytes);
        if (result != 0) (void)memcpy(destination, bytes, 16);
        return result;
    }
    return eos_socket_fail(EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED);
}

const char *eos_rust_inet_ntop(int32_t family, const void *source,
                               char *destination,
                               eos_rust_socklen_t capacity) {
    char formatted[40];
    uint32_t length;
    if (source == NULL || destination == NULL) {
        (void)eos_socket_fail(EOS_ERRNO_FAULT);
        return NULL;
    }
    if (family == EOS_RUST_AF_INET) {
        length = eos_addrinfo_format_ipv4((const uint8_t *)source, formatted);
    } else if (family == EOS_RUST_AF_INET6) {
        length = eos_addrinfo_format_ipv6((const uint8_t *)source, formatted);
    } else {
        (void)eos_socket_fail(EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED);
        return NULL;
    }
    if (capacity <= length) {
        (void)eos_socket_fail(EOS_ERRNO_NO_SPACE);
        return NULL;
    }
    (void)memcpy(destination, formatted, length + 1U);
    return destination;
}

static uint16_t eos_addrinfo_network_u16(uint16_t value) {
    return (uint16_t)((value << 8U) | (value >> 8U));
}

static int32_t eos_addrinfo_service(const char *service, uint16_t *port) {
    uint32_t value = 0;
    const char *cursor;
    if (service == NULL) {
        *port = 0;
        return 0;
    }
    if (*service == '\0') return EOS_RUST_EAI_SERVICE;
    cursor = service;
    while (*cursor != '\0') {
        if (*cursor < '0' || *cursor > '9') {
            *eos_tls_errno_location() = EOS_ERRNO_NOT_SUPPORTED;
            return EOS_RUST_EAI_SYSTEM;
        }
        if (value > (UINT16_MAX - (uint32_t)(*cursor - '0')) / 10U) {
            return EOS_RUST_EAI_SERVICE;
        }
        value = value * 10U + (uint32_t)(*cursor - '0');
        ++cursor;
    }
    *port = eos_addrinfo_network_u16((uint16_t)value);
    return 0;
}

static void eos_addrinfo_destroy_chain(eos_rust_addrinfo *result) {
    while (result != NULL) {
        eos_rust_addrinfo *next = result->ai_next;
        eos_rust_free(result->ai_canonname);
        eos_rust_free(result->ai_addr);
        eos_rust_free(result);
        result = next;
    }
}

static int32_t eos_addrinfo_append(eos_rust_addrinfo **head,
                                   eos_rust_addrinfo ***tail,
                                   int32_t flags,
                                   int32_t family,
                                   int32_t socktype,
                                   int32_t protocol,
                                   uint16_t port,
                                   const uint8_t address[16],
                                   const char *canonical) {
    eos_rust_addrinfo *entry =
        (eos_rust_addrinfo *)eos_rust_calloc(1, (uint32_t)sizeof(*entry));
    uint32_t address_size = family == EOS_RUST_AF_INET
                                ? (uint32_t)sizeof(eos_rust_sockaddr_in)
                                : (uint32_t)sizeof(eos_rust_sockaddr_in6);
    if (entry == NULL) return EOS_RUST_EAI_MEMORY;
    entry->ai_addr = (eos_rust_sockaddr *)eos_rust_calloc(1, address_size);
    if (entry->ai_addr == NULL) {
        eos_rust_free(entry);
        return EOS_RUST_EAI_MEMORY;
    }
    entry->ai_flags = flags;
    entry->ai_family = family;
    entry->ai_socktype = socktype;
    entry->ai_protocol = protocol;
    entry->ai_addrlen = address_size;
    if (family == EOS_RUST_AF_INET) {
        eos_rust_sockaddr_in *value = (eos_rust_sockaddr_in *)entry->ai_addr;
        value->sin_family = EOS_RUST_AF_INET;
        value->sin_port = port;
        (void)memcpy(&value->sin_addr.s_addr, address, 4);
    } else {
        eos_rust_sockaddr_in6 *value = (eos_rust_sockaddr_in6 *)entry->ai_addr;
        value->sin6_family = EOS_RUST_AF_INET6;
        value->sin6_port = port;
        (void)memcpy(value->sin6_addr.s6_addr, address, 16);
    }
    if (canonical != NULL) {
        uint32_t length = (uint32_t)strlen(canonical) + 1U;
        entry->ai_canonname = (char *)eos_rust_malloc(length);
        if (entry->ai_canonname == NULL) {
            eos_addrinfo_destroy_chain(entry);
            return EOS_RUST_EAI_MEMORY;
        }
        (void)memcpy(entry->ai_canonname, canonical, length);
    }
    if (*head == NULL) *head = entry;
    **tail = entry;
    *tail = &entry->ai_next;
    return 0;
}

int32_t eos_rust_getaddrinfo(const char *node, const char *service,
                             const eos_rust_addrinfo *hints,
                             eos_rust_addrinfo **result) {
    eos_rust_addrinfo defaults;
    eos_rust_addrinfo *head = NULL;
    eos_rust_addrinfo **tail = &head;
    uint8_t address[16];
    uint8_t families[2];
    uint8_t addresses[2][16];
    uint32_t family_count = 0;
    uint32_t family_index;
    int32_t types[2];
    int32_t protocols[2];
    uint32_t type_count = 0;
    uint32_t type_index;
    uint16_t port;
    int32_t error;
    const int32_t supported_flags = EOS_RUST_AI_PASSIVE |
                                    EOS_RUST_AI_CANONNAME |
                                    EOS_RUST_AI_NUMERICHOST |
                                    EOS_RUST_AI_NUMERICSERV;
    if (result == NULL) return EOS_RUST_EAI_FAIL;
    *result = NULL;
    if (node == NULL && service == NULL) return EOS_RUST_EAI_NONAME;
    (void)memset(&defaults, 0, sizeof(defaults));
    if (hints == NULL) hints = &defaults;
    if ((hints->ai_flags & ~supported_flags) != 0) {
        return EOS_RUST_EAI_BADFLAGS;
    }
    if (hints->ai_family != EOS_RUST_AF_UNSPEC &&
        hints->ai_family != EOS_RUST_AF_INET &&
        hints->ai_family != EOS_RUST_AF_INET6) {
        return EOS_RUST_EAI_FAMILY;
    }
    if (hints->ai_socktype != 0 &&
        hints->ai_socktype != EOS_RUST_SOCK_STREAM &&
        hints->ai_socktype != EOS_RUST_SOCK_DGRAM) {
        return EOS_RUST_EAI_SOCKTYPE;
    }
    if (hints->ai_protocol != 0 &&
        hints->ai_protocol != EOS_RUST_IPPROTO_TCP &&
        hints->ai_protocol != EOS_RUST_IPPROTO_UDP) {
        return EOS_RUST_EAI_SOCKTYPE;
    }
    if ((hints->ai_socktype == EOS_RUST_SOCK_STREAM &&
         hints->ai_protocol == EOS_RUST_IPPROTO_UDP) ||
        (hints->ai_socktype == EOS_RUST_SOCK_DGRAM &&
         hints->ai_protocol == EOS_RUST_IPPROTO_TCP)) {
        return EOS_RUST_EAI_SOCKTYPE;
    }
    error = eos_addrinfo_service(service, &port);
    if (error != 0) return error;
    if (node == NULL) {
        if (hints->ai_family != EOS_RUST_AF_INET) {
            families[family_count] = EOS_RUST_AF_INET6;
            (void)memset(addresses[family_count], 0, 16);
            if ((hints->ai_flags & EOS_RUST_AI_PASSIVE) == 0) {
                addresses[family_count][15] = 1;
            }
            ++family_count;
        }
        if (hints->ai_family != EOS_RUST_AF_INET6) {
            families[family_count] = EOS_RUST_AF_INET;
            (void)memset(addresses[family_count], 0, 16);
            if ((hints->ai_flags & EOS_RUST_AI_PASSIVE) == 0) {
                addresses[family_count][0] = 127;
                addresses[family_count][3] = 1;
            }
            ++family_count;
        }
    } else if (eos_addrinfo_parse_ipv4(node, address)) {
        if (hints->ai_family == EOS_RUST_AF_INET6) return EOS_RUST_EAI_NONAME;
        families[0] = EOS_RUST_AF_INET;
        (void)memset(addresses[0], 0, 16);
        (void)memcpy(addresses[0], address, 4);
        family_count = 1;
    } else if (eos_addrinfo_parse_ipv6(node, address)) {
        if (hints->ai_family == EOS_RUST_AF_INET) return EOS_RUST_EAI_NONAME;
        families[0] = EOS_RUST_AF_INET6;
        (void)memcpy(addresses[0], address, 16);
        family_count = 1;
    } else if ((hints->ai_flags & EOS_RUST_AI_NUMERICHOST) != 0) {
        return EOS_RUST_EAI_NONAME;
    } else {
        *eos_tls_errno_location() = EOS_ERRNO_NOT_SUPPORTED;
        return EOS_RUST_EAI_SYSTEM;
    }
    if (hints->ai_socktype == EOS_RUST_SOCK_STREAM ||
        hints->ai_protocol == EOS_RUST_IPPROTO_TCP) {
        types[0] = EOS_RUST_SOCK_STREAM;
        protocols[0] = EOS_RUST_IPPROTO_TCP;
        type_count = 1;
    } else if (hints->ai_socktype == EOS_RUST_SOCK_DGRAM ||
               hints->ai_protocol == EOS_RUST_IPPROTO_UDP) {
        types[0] = EOS_RUST_SOCK_DGRAM;
        protocols[0] = EOS_RUST_IPPROTO_UDP;
        type_count = 1;
    } else {
        types[0] = EOS_RUST_SOCK_STREAM;
        protocols[0] = EOS_RUST_IPPROTO_TCP;
        types[1] = EOS_RUST_SOCK_DGRAM;
        protocols[1] = EOS_RUST_IPPROTO_UDP;
        type_count = 2;
    }
    for (family_index = 0; family_index < family_count; ++family_index) {
        for (type_index = 0; type_index < type_count; ++type_index) {
            const char *canonical =
                head == NULL && (hints->ai_flags & EOS_RUST_AI_CANONNAME) != 0
                    ? node : NULL;
            error = eos_addrinfo_append(&head, &tail, hints->ai_flags,
                                        families[family_index],
                                        types[type_index],
                                        protocols[type_index], port,
                                        addresses[family_index], canonical);
            if (error != 0) {
                eos_addrinfo_destroy_chain(head);
                return error;
            }
        }
    }
    *result = head;
    return 0;
}

void eos_rust_freeaddrinfo(eos_rust_addrinfo *result) {
    eos_addrinfo_destroy_chain(result);
}

const char *eos_rust_gai_strerror(int32_t error_code) {
    switch (error_code) {
    case 0: return "success";
    case EOS_RUST_EAI_BADFLAGS: return "invalid address-information flags";
    case EOS_RUST_EAI_NONAME: return "name or service is not numeric";
    case EOS_RUST_EAI_AGAIN: return "temporary resolver failure";
    case EOS_RUST_EAI_FAIL: return "resolver failure";
    case EOS_RUST_EAI_FAMILY: return "unsupported address family";
    case EOS_RUST_EAI_SOCKTYPE: return "unsupported socket type";
    case EOS_RUST_EAI_SERVICE: return "unsupported service";
    case EOS_RUST_EAI_MEMORY: return "address allocation failed";
    case EOS_RUST_EAI_SYSTEM: return "system error";
    case EOS_RUST_EAI_OVERFLOW: return "address result overflow";
    default: return "unknown address-information error";
    }
}
