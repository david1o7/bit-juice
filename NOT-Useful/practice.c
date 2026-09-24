#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
   char *data;
   size_t length;
   size_t capacity;
} Dynstr;

Dynstr *create_dynstr(size_t initial_cap){
    Dynstr *sb = malloc(sizeof(Dynstr));
    if (!sb) return NULL;

    sb->capacity = initial_cap;
    sb->length = 0;
    sb->data = malloc(sb->capacity);

    if (!sb->data){
        free(sb);
        return NULL;
    }

    sb->data[0] = '\0';

    return sb;
}   

int dynstr_append(Dynstr *sb, const char *value){
    if (!sb || !value){
        return -1;
    }
    size_t len = strlen(value);
    int space = (sb->length > 0);
    size_t needed = sb->length + space + len + 1;

    if (needed > sb->capacity){
        size_t new_cap = sb->capacity * 2;

        if (needed > new_cap){
            new_cap = needed;
        }

        char *temp = realloc(sb->data, new_cap);
        if (!temp) return -1;

        sb->data = temp;
        sb->capacity = new_cap;
    }

    if (space){
        sb->data[sb->length]= ' ';
        sb->length++;
    }

    memcpy(sb->data + sb->length,value,len);
    sb->length += len;
    sb->data[sb->length] = '\0';
    return 0;
}

void dynstr_free(Dynstr *sb){
    if (!sb) return NULL;
    free(sb->data);
    free(sb);
}

int main(void) {
    char input[50];
    Dynstr *my_str = create_dynstr(8);
    if (!my_str) return 1;

    dynstr_append(my_str, "Hello");
    printf("First Append: %s, Length: %zu, Capacity: %zu\n", my_str->data, my_str->length, my_str->capacity);

    printf("Enter a message or preferably, you name:\n");
    if (fgets(input, sizeof(input), stdin) == NULL){
        fprintf(stderr,"Error occurred whrn reading user input\n");
        return 1;
    }

    input[strcspn(input, "\n")] = '\0';

    dynstr_append(my_str, input);
    printf("Final Message: %s, Length: %zu, Capacity: %zu\n", my_str->data, my_str->length, my_str->capacity);

    dynstr_free(my_str);
    return 0;
}