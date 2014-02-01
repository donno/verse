/*
 *
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 * Contributor(s): Jiri Hnidek <jiri.hnidek@tul.cz>.
 *
 */

#include <string.h>

#ifdef WITH_OPENSSL
#include <openssl/sha.h>
#ifdef __APPLE__
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#endif

#include "vs_main.h"
#include "v_context.h"
#include "v_common.h"

/**
 * \brief This function tries to authenticate user. The username is compared
 * with records from CSV file (this file is read after start of the server).
 * When username and password match, then 1 is returned otherwise 0 is
 * returned.
 */
int vs_csv_auth_user(struct vContext *C, const char *username, const char *pass)
{
	struct VS_CTX *vs_ctx = CTX_server_ctx(C);
	struct VSUser *vsuser;
	int uid = -1;

	vsuser = vs_ctx->users.first;

	while(vsuser) {
		/* Try to find record with this username. (the username has to be
		 * unique). The user could not be fake user (super user and other users) */
		if( vsuser->fake_user != 1) {
			if( strcmp(vsuser->username, username) == 0 ) {
				/* When record with username, then passwords has to be same,
				 * otherwise return 0 */
				if(vsuser->password != NULL) {
					if( strcmp(vsuser->password, pass) == 0 ) {
						uid = vsuser->user_id;
					}
				} else if(vsuser->password_hash != NULL) {
					/* Compute SHA1 hash of pass and compare it with
					 * password_hash */
#ifdef WITH_OPENSSL
					int i;
					unsigned char md[SHA_DIGEST_LENGTH];
					char pass_hash[2 * SHA_DIGEST_LENGTH];
					
					/* Generate SHA1 from the received password */
					SHA1((unsigned char*)pass, strlen(pass), md);

					/* Convert SHA1 hash to hexadecimal string, which could be
					 * generated by following command:
					 *
					 * echo -n "my_secret_pass" | openssl dgst -sha1
					 *
					 */
					for(i=0; i<SHA_DIGEST_LENGTH; i++) {
						sprintf(&pass_hash[2*i], "%02x", md[i]);
					}
					
					/* Compare hashes */
					if (strncmp(vsuser->password_hash, pass_hash, 2*SHA_DIGEST_LENGTH) == 0) {
						uid = vsuser->user_id;
					}
#else
					v_print_log(VRS_PRINT_WARNING,
							"Can't compare SHA1 hashes without OpenSSL");
#endif
				}
				break;
			}
		}
		vsuser = vsuser->next;
	}

	return uid;
}

/**
 * \brief Load user accounts from CSV file
 */
int vs_load_user_accounts_csv_file(VS_CTX *vs_ctx)
{
	int raw = 0, col, prev_comma, next_comma;
	int ret = 0, usr_count = 0;
	FILE *file;

	if(vs_ctx->csv_user_file != NULL) {
		file = fopen(vs_ctx->csv_user_file, "rt");
		if(file != NULL) {
			struct VSUser *new_user, *user;
			char line[LINE_LEN], *tmp;
			char raw_pass = 0, hash_pass = 0;

			raw = 0;
			while( fgets(line, LINE_LEN-2, file) != NULL) {
				/* First line has to have following structure:
				 * username,password,UID,real name
				 * or
				 * username,passhash,UID,real name*/
				if(raw == 0) {
					int offset = 0;
					if(strncmp(&line[offset], "username", 8) != 0) break;
					offset += 9;
					if(strncmp(&line[offset], "password", 8) == 0) {
						raw_pass = 1;
					} else if(strncmp(&line[offset], "passhash", 8) == 0) {
						hash_pass = 1;
					} else {
						break;
					}
					offset += 9;
					if(strncmp(&line[offset], "UID", 3) != 0) break;
					offset += 4;
					if(strncmp(&line[offset], "real name", 9) != 0) break;
				} else {
					new_user = (struct VSUser*)calloc(1, sizeof(struct VSUser));

					/* username */
					prev_comma = next_comma = 0;
					for(col=0; col < LINE_LEN && line[col] != ','; col++) {}
					next_comma = col;
					new_user->username = strndup(&line[prev_comma], next_comma-prev_comma);
					prev_comma = next_comma+1;

					/* password */
					for(col=prev_comma; col < LINE_LEN && line[col] != ','; col++) {}
					next_comma = col;
					if (raw_pass == 1) {
						new_user->password = strndup(&line[prev_comma], next_comma-prev_comma);
						new_user->password_hash = NULL;
					} else if (hash_pass == 1) {
						new_user->password = NULL;
						new_user->password_hash = strndup(&line[prev_comma], next_comma-prev_comma);
					}
					prev_comma = next_comma+1;
					
					/* user id */
					for(col=prev_comma; col < LINE_LEN && line[col] != ','; col++) {}
					next_comma = col;
					tmp = strndup(&line[prev_comma], next_comma-prev_comma);
					sscanf(tmp, "%d", (int*)&new_user->user_id);
					free(tmp);
					prev_comma = next_comma+1;

					/* real name */
					for(col=prev_comma; col < LINE_LEN && line[col] != '\n'; col++) {}
					next_comma = col;
					new_user->realname = strndup(&line[prev_comma], next_comma-prev_comma);

					/* This is real user and can login */
					new_user->fake_user = 0;

					/* Check uniqueness of username and user id */
					user = vs_ctx->users.first;
					while(user != NULL) {
						/* User ID */
						if(user->user_id == new_user->user_id) {
							v_print_log(VRS_PRINT_WARNING, "User %s could not be added to list of user, because user %s has same user ID: %d\n",
									new_user->username, user->username, user->user_id);
							break;
						}
						/* Username */
						if( strcmp(user->username, new_user->username) == 0) {
							v_print_log(VRS_PRINT_WARNING, "User %s could not be added to list of user, because user ID: %d has the same name\n",
									new_user->username, user->user_id);
							break;
						}
						user = user->next;
					}

					/* Check correctness of User ID */
					if(!(new_user->user_id >= MIN_USER_ID && new_user->user_id <= MAX_USER_ID)) {
						v_print_log(VRS_PRINT_WARNING, "User ID: %d of user: %s not in valid range (%d-%d)\n",
								new_user->user_id, new_user->username, MIN_USER_ID, MAX_USER_ID);
						free(new_user);
					} else {
						if(user == NULL) {
							v_list_add_tail(&vs_ctx->users, (void*)new_user);
							v_print_log(VRS_PRINT_DEBUG_MSG, "Added: username: %s, ID: %d, realname: %s\n",
									new_user->username, new_user->user_id, new_user->realname);
							usr_count++;
						} else {
							free(new_user);
						}
					}

				}
				raw++;
			}
			fclose(file);

			if(usr_count > 0) {
				ret = 1;
				v_print_log(VRS_PRINT_DEBUG_MSG, "%d user account loaded from file: %s\n",
							usr_count, vs_ctx->csv_user_file);
			} else {
				ret = 0;
				v_print_log(VRS_PRINT_ERROR, "No valid user account loaded from file: %s\n",
						vs_ctx->csv_user_file);
			}
		} else {
			v_print_log(VRS_PRINT_ERROR, "Could not open file: %s\n",
						vs_ctx->csv_user_file);
		}
	}

	return ret;
}
