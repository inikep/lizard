// LZ5frame API example : compress a file
// Based on sample code from Zbigniew Jędrzejewski-Szmek

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <lz5frame.h>

#define BUF_SIZE (16*1024)
#define LIZARD_HEADER_SIZE 19
#define LZ5_FOOTER_SIZE 4

static const LZ5F_preferences_t lz5_preferences = {
	{ LZ5F_max256KB, LZ5F_blockLinked, LZ5F_noContentChecksum, LZ5F_frame, 0, { 0, 0 } },
	0,   /* compression level */
	0,   /* autoflush */
	{ 0, 0, 0, 0 },  /* reserved, must be set to 0 */
};

static int compress_file(FILE *in, FILE *out, size_t *size_in, size_t *size_out) {
	LZ5F_errorCode_t r;
	LZ5F_compressionContext_t ctx;
	char *src, *buf = NULL;
	size_t size, n, k, count_in = 0, count_out, offset = 0, frame_size;

	r = LZ5F_createCompressionContext(&ctx, LZ5F_VERSION);
	if (LZ5F_isError(r)) {
		printf("Failed to create context: error %zu", r);
		return 1;
	}
	r = 1;

	src = malloc(BUF_SIZE);
	if (!src) {
		printf("Not enough memory");
		goto cleanup;
	}

	frame_size = LZ5F_compressBound(BUF_SIZE, &lz5_preferences);
	size =  frame_size + LIZARD_HEADER_SIZE + LZ5_FOOTER_SIZE;
	buf = malloc(size);
	if (!buf) {
		printf("Not enough memory");
		goto cleanup;
	}

	n = offset = count_out = LZ5F_compressBegin(ctx, buf, size, &lz5_preferences);
	if (LZ5F_isError(n)) {
		printf("Failed to start compression: error %zu", n);
		goto cleanup;
	}

	printf("Buffer size is %zu bytes, header size %zu bytes\n", size, n);

	for (;;) {
		k = fread(src, 1, BUF_SIZE, in);
		if (k == 0)
			break;
		count_in += k;

		n = LZ5F_compressUpdate(ctx, buf + offset, size - offset, src, k, NULL);
		if (LZ5F_isError(n)) {
			printf("Compression failed: error %zu", n);
			goto cleanup;
		}

		offset += n;
		count_out += n;
		if (size - offset < frame_size + LZ5_FOOTER_SIZE) {
			printf("Writing %zu bytes\n", offset);

			k = fwrite(buf, 1, offset, out);
			if (k < offset) {
				if (ferror(out))
					printf("Write failed");
				else
					printf("Short write");
				goto cleanup;
			}

			offset = 0;
		}
	}

	n = LZ5F_compressEnd(ctx, buf + offset, size - offset, NULL);
	if (LZ5F_isError(n)) {
		printf("Failed to end compression: error %zu", n);
		goto cleanup;
	}

	offset += n;
	count_out += n;
	printf("Writing %zu bytes\n", offset);

	k = fwrite(buf, 1, offset, out);
	if (k < offset) {
		if (ferror(out))
			printf("Write failed");
		else
			printf("Short write");
		goto cleanup;
	}

	*size_in = count_in;
	*size_out = count_out;
	r = 0;
 cleanup:
	if (ctx)
		LZ5F_freeCompressionContext(ctx);
	free(src);
	free(buf);
	return r;
}

static int compress(const char *input, const char *output) {
	char *tmp = NULL;
	FILE *in = NULL, *out = NULL;
	size_t size_in = 0, size_out = 0;
	int r = 1;

	if (!output) {
		size_t len = strlen(input);

		output = tmp = malloc(len + 5);
		if (!tmp) {
			printf("Not enough memory");
			return 1;
		}
		strcpy(tmp, input);
		strcpy(tmp + len, ".lz5");
	}

	in = fopen(input, "rb");
	if (!in) {
		fprintf(stderr, "Failed to open input file %s: %s\n", input, strerror(errno));
		goto cleanup;
	}

	out = fopen(output, "wb");
	if (!out) {
		fprintf(stderr, "Failed to open output file %s: %s\n", output, strerror(errno));
		goto cleanup;
	}

	r = compress_file(in, out, &size_in, &size_out);
	if (r == 0)
		printf("%s: %zu → %zu bytes, %.1f%%\n",
		       input, size_in, size_out,
		       (double)size_out / size_in * 100);
 cleanup:
	if (in)
		fclose(in);
	if (out)
		fclose(out);
	free(tmp);
	return r;
}


int main(int argc, char **argv) {
	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Syntax: %s <input> <output>\n", argv[0]);
		return EXIT_FAILURE;
	}

	return compress(argv[1], argv[2]);
}
