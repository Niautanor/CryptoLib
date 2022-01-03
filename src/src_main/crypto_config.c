/* Copyright (C) 2009 - 2022 National Aeronautics and Space Administration.
   All Foreign Rights are Reserved to the U.S. Government.

   This software is provided "as is" without any warranty of any kind, either expressed, implied, or statutory,
   including, but not limited to, any warranty that the software will conform to specifications, any implied warranties
   of merchantability, fitness for a particular purpose, and freedom from infringement, and any warranty that the
   documentation will conform to the program, or any warranty that the software will be error free.

   In no event shall NASA be liable for any damages, including, but not limited to direct, indirect, special or
   consequential damages, arising out of, resulting from, or in any way connected with the software or its
   documentation, whether or not based upon warranty, contract, tort or otherwise, and whether or not loss was sustained
   from, or arose out of the results of, or use of, the software, documentation or services provided hereunder.

   ITC Team
   NASA IV&V
   jstar-development-team@mail.nasa.gov
*/

/*
** Includes
*/
#include "crypto.h"

/*
** Initialization Functions
*/

/**
 * @brief Function: Crypto_Init_Unit_test
 * @return int32: status
 **/
int32_t Crypto_Init_Unit_Test(void)
{
    int32_t status = CRYPTO_LIB_SUCCESS;
    Crypto_Config_CryptoLib(SADB_TYPE_INMEMORY, CRYPTO_TC_CREATE_FECF_TRUE, TC_PROCESS_SDLS_PDUS_TRUE, TC_HAS_PUS_HDR,
                            TC_IGNORE_SA_STATE_FALSE, TC_IGNORE_ANTI_REPLAY_FALSE, TC_UNIQUE_SA_PER_MAP_ID_FALSE,
                            TC_CHECK_FECF_TRUE, 0x3F);
    Crypto_Config_Add_Gvcid_Managed_Parameter(0, 0x0003, 0, TC_HAS_FECF, TC_HAS_SEGMENT_HDRS);
    Crypto_Config_Add_Gvcid_Managed_Parameter(0, 0x0003, 1, TC_HAS_FECF, TC_HAS_SEGMENT_HDRS);
    status = Crypto_Init();
    return status;
}

/**
 * @brief Function: Crypto_Init_With_Configs
 * @param crypto_config_p: CryptoConfig_t*
 * @param gvcid_managed_parameters_p: GvcidManagedParameters_t*
 * @param sadb_mariadb_config_p: SadbMariaDBConfig_t*
 * @return int32: Success/Failure
 **/
int32_t Crypto_Init_With_Configs(CryptoConfig_t *crypto_config_p, GvcidManagedParameters_t *gvcid_managed_parameters_p,
                                 SadbMariaDBConfig_t *sadb_mariadb_config_p)
{
    int32_t status = CRYPTO_LIB_SUCCESS;
    crypto_config = crypto_config_p;
    gvcid_managed_parameters = gvcid_managed_parameters_p;
    sadb_mariadb_config = sadb_mariadb_config_p;
    status = Crypto_Init();
    return status;
}

/**
 * @brief Function Crypto_Init
 * Initializes libgcrypt, Security Associations
 **/
int32_t Crypto_Init(void)
{
    int32_t status = CRYPTO_LIB_SUCCESS;

    if (crypto_config == NULL)
    {
        status = CRYPTO_CONFIGURATION_NOT_COMPLETE;
        printf(KRED "ERROR: CryptoLib must be configured before intializing!\n" RESET);
        return status; // No configuration set -- return!
    }
    if (gvcid_managed_parameters == NULL)
    {
        status = CRYPTO_MANAGED_PARAM_CONFIGURATION_NOT_COMPLETE;
        printf(KRED "ERROR: CryptoLib  Managed Parameters must be configured before intializing!\n" RESET);
        return status; // No Managed Parameter configuration set -- return!
    }

#ifdef TC_DEBUG
    Crypto_mpPrint(gvcid_managed_parameters, 1);
#endif

    // Prepare SADB type from config
    if (crypto_config->sadb_type == SADB_TYPE_INMEMORY)
    {
        sadb_routine = get_sadb_routine_inmemory();
    }
    else if (crypto_config->sadb_type == SADB_TYPE_MARIADB)
    {
        if (sadb_mariadb_config == NULL)
        {
            status = CRYPTO_MARIADB_CONFIGURATION_NOT_COMPLETE;
            printf(KRED "ERROR: CryptoLib MariaDB must be configured before intializing!\n" RESET);
            return status; // MariaDB connection specified but no configuration exists, return!
        }
        sadb_routine = get_sadb_routine_mariadb();
    }
    else
    {
        status = SADB_INVALID_SADB_TYPE;
        return status;
    } // TODO: Error stack

    // Initialize libgcrypt
    if (!gcry_check_version(GCRYPT_VERSION))
    {
        fprintf(stderr, "Gcrypt Version: %s", GCRYPT_VERSION);
        printf(KRED "\tERROR: gcrypt version mismatch! \n" RESET);
    }
    if (gcry_control(GCRYCTL_SELFTEST) != GPG_ERR_NO_ERROR)
    {
        printf(KRED "ERROR: gcrypt self test failed\n" RESET);
    }
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

    // Init Security Associations
    status = sadb_routine->sadb_init();
    status = sadb_routine->sadb_config();

    Crypto_Local_Init();
    Crypto_Local_Config();

    // TODO - Add error checking

    // Init table for CRC calculations
    Crypto_Calc_CRC_Init_Table();

    // cFS Standard Initialized Message
    printf(KBLU "Crypto Lib Intialized.  Version %d.%d.%d.%d\n" RESET, CRYPTO_LIB_MAJOR_VERSION,
           CRYPTO_LIB_MINOR_VERSION, CRYPTO_LIB_REVISION, CRYPTO_LIB_MISSION_REV);

    return status;
}

/**
 * @brief Function: Crypto_Shutdown
 * Free memory objects & restore pointers to NULL for re-initialization
 * @return int32: Success/Failure
 **/
int32_t Crypto_Shutdown(void)
{
    int32_t status = CRYPTO_LIB_SUCCESS;

    if (crypto_config != NULL)
    {
        free(crypto_config);
        crypto_config = NULL;
    }
    if (sadb_mariadb_config != NULL)
    {
        free(sadb_mariadb_config);
        sadb_mariadb_config = NULL;
    }
    current_managed_parameters = NULL;

    if (gvcid_managed_parameters != NULL)
    {
        Crypto_Free_Managed_Parameters(gvcid_managed_parameters);
        gvcid_managed_parameters = NULL;
    }

    return status;
}

/**
 * @brief Function: Crypto_Config_CryptoLib
 * @param sadb_type: uint8
 * @param crypto_create_fecf: uint8
 * @param process_sdls_pdus: uint8
 * @param has_pus_hdr: uint8
 * @param ignore_sa_state: uint8
 * @param ignore_anti_replay: uint8
 * @param unique_sa_per_mapid: uint8
 * @param crypto_check_fecf: uint8
 * @param vcid_bitmask: uint8
 * @return int32: Success/Failure
 **/
int32_t Crypto_Config_CryptoLib(uint8_t sadb_type, uint8_t crypto_create_fecf, uint8_t process_sdls_pdus,
                                uint8_t has_pus_hdr, uint8_t ignore_sa_state, uint8_t ignore_anti_replay,
                                uint8_t unique_sa_per_mapid, uint8_t crypto_check_fecf, uint8_t vcid_bitmask)
{
    int32_t status = CRYPTO_LIB_SUCCESS;
    crypto_config = (CryptoConfig_t *)calloc(1, CRYPTO_CONFIG_SIZE);
    crypto_config->sadb_type = sadb_type;
    crypto_config->crypto_create_fecf = crypto_create_fecf;
    crypto_config->process_sdls_pdus = process_sdls_pdus;
    crypto_config->has_pus_hdr = has_pus_hdr;
    crypto_config->ignore_sa_state = ignore_sa_state;
    crypto_config->ignore_anti_replay = ignore_anti_replay;
    crypto_config->unique_sa_per_mapid = unique_sa_per_mapid;
    crypto_config->crypto_check_fecf = crypto_check_fecf;
    crypto_config->vcid_bitmask = vcid_bitmask;
    return status;
}

/**
 * @brief Function: Crypto_Config_MariaDB
 * @param mysql_username: char*
 * @param mysql_password: char*
 * @param mysql_hostname: char*
 * @param mysql_database: char*
 * @param mysql_port: uint16
 * @return int32: Success/Failure
 **/
int32_t Crypto_Config_MariaDB(char *mysql_username, char *mysql_password, char *mysql_hostname,
                              char *mysql_database, uint16_t mysql_port)
{
    int32_t status = CRYPTO_LIB_SUCCESS;
    sadb_mariadb_config = (SadbMariaDBConfig_t *)calloc(1, SADB_MARIADB_CONFIG_SIZE);
    if(sadb_mariadb_config != NULL)
    {
        sadb_mariadb_config->mysql_username = mysql_username;
        sadb_mariadb_config->mysql_password = mysql_password;
        sadb_mariadb_config->mysql_hostname = mysql_hostname;
        sadb_mariadb_config->mysql_database = mysql_database;
        sadb_mariadb_config->mysql_port = mysql_port;    
    }
    else
    {
        // null returned, throw error and return
        status = CRYPTO_LIB_ERR_NULL_BUFFER;
    }

    return status;
}

/**
 * @brief Function: Crypto_Config_Add_Gvcid_Managed_Parameter
 * @param tfvn: uint8
 * @param scid: uint16
 * @param vcid: uint8
 * @param has_fecf: uint8
 * @param has_segmentation_hdr: uint8
 * @return int32: Success/Failure
 **/
int32_t Crypto_Config_Add_Gvcid_Managed_Parameter(uint8_t tfvn, uint16_t scid, uint8_t vcid, uint8_t has_fecf,
                                                  uint8_t has_segmentation_hdr)
{
    int32_t status = CRYPTO_LIB_SUCCESS;

    if (gvcid_managed_parameters == NULL)
    { // case: Global Root Node not Set
        gvcid_managed_parameters = (GvcidManagedParameters_t *)calloc(1, GVCID_MANAGED_PARAMETERS_SIZE);
        if(gvcid_managed_parameters != NULL)
        {
            gvcid_managed_parameters->tfvn = tfvn;
            gvcid_managed_parameters->scid = scid;
            gvcid_managed_parameters->vcid = vcid;
            gvcid_managed_parameters->has_fecf = has_fecf;
            gvcid_managed_parameters->has_segmentation_hdr = has_segmentation_hdr;
            gvcid_managed_parameters->next = NULL;
            return status;
        }
        else
        {
            // calloc failed - return error
            status = CRYPTO_LIB_ERR_NULL_BUFFER;
            return status;
        }
    }
    else
    { // Recurse through nodes and add at end
        return crypto_config_add_gvcid_managed_parameter_recursion(tfvn, scid, vcid, has_fecf, has_segmentation_hdr,
                                                                   gvcid_managed_parameters);
    }
}

/**
 * @brief Function: crypto_config_add_gvcid_managed_parameter_recursion
 * @param tfvn: uint8
 * @param scid: uint16
 * @param vcid: uint8
 * @param has_fecf: uint8
 * @param has_segmentation_hdr: uint8
 * @param managed_parameter: GvcidManagedParameters_t*
 * @return int32: Success/Failure
 **/
int32_t crypto_config_add_gvcid_managed_parameter_recursion(uint8_t tfvn, uint16_t scid, uint8_t vcid, uint8_t has_fecf,
                                                            uint8_t has_segmentation_hdr,
                                                            GvcidManagedParameters_t *managed_parameter)
{
    if (managed_parameter->next != NULL)
    {
        return crypto_config_add_gvcid_managed_parameter_recursion(tfvn, scid, vcid, has_fecf, has_segmentation_hdr,
                                                                   managed_parameter->next);
    }
    else
    {
        managed_parameter->next = (GvcidManagedParameters_t *)calloc(1, GVCID_MANAGED_PARAMETERS_SIZE);
        managed_parameter->next->tfvn = tfvn;
        managed_parameter->next->scid = scid;
        managed_parameter->next->vcid = vcid;
        managed_parameter->next->has_fecf = has_fecf;
        managed_parameter->next->has_segmentation_hdr = has_segmentation_hdr;
        managed_parameter->next->next = NULL;
        return CRYPTO_LIB_SUCCESS;
    }
}

/**
 * @brief Function: Crypto_Local_Config
 * Initalizes TM Configuration, Log, and Keyrings
 **/
void Crypto_Local_Config(void)
{
    // Initial TM configuration
    tm_frame.tm_sec_header.spi = 1;

    // Initialize Log
    log_summary.num_se = 2;
    log_summary.rs = LOG_SIZE;
    // Add a two messages to the log
    log_summary.rs--;
    mc_log.blk[log_count].emt = STARTUP;
    mc_log.blk[log_count].emv[0] = 0x4E;
    mc_log.blk[log_count].emv[1] = 0x41;
    mc_log.blk[log_count].emv[2] = 0x53;
    mc_log.blk[log_count].emv[3] = 0x41;
    mc_log.blk[log_count++].em_len = 4;
    log_summary.rs--;
    mc_log.blk[log_count].emt = STARTUP;
    mc_log.blk[log_count].emv[0] = 0x4E;
    mc_log.blk[log_count].emv[1] = 0x41;
    mc_log.blk[log_count].emv[2] = 0x53;
    mc_log.blk[log_count].emv[3] = 0x41;
    mc_log.blk[log_count++].em_len = 4;

    // Master Keys
    // 0 - 000102030405060708090A0B0C0D0E0F000102030405060708090A0B0C0D0E0F -> ACTIVE
    ek_ring[0].value[0] = 0x00;
    ek_ring[0].value[1] = 0x01;
    ek_ring[0].value[2] = 0x02;
    ek_ring[0].value[3] = 0x03;
    ek_ring[0].value[4] = 0x04;
    ek_ring[0].value[5] = 0x05;
    ek_ring[0].value[6] = 0x06;
    ek_ring[0].value[7] = 0x07;
    ek_ring[0].value[8] = 0x08;
    ek_ring[0].value[9] = 0x09;
    ek_ring[0].value[10] = 0x0A;
    ek_ring[0].value[11] = 0x0B;
    ek_ring[0].value[12] = 0x0C;
    ek_ring[0].value[13] = 0x0D;
    ek_ring[0].value[14] = 0x0E;
    ek_ring[0].value[15] = 0x0F;
    ek_ring[0].value[16] = 0x00;
    ek_ring[0].value[17] = 0x01;
    ek_ring[0].value[18] = 0x02;
    ek_ring[0].value[19] = 0x03;
    ek_ring[0].value[20] = 0x04;
    ek_ring[0].value[21] = 0x05;
    ek_ring[0].value[22] = 0x06;
    ek_ring[0].value[23] = 0x07;
    ek_ring[0].value[24] = 0x08;
    ek_ring[0].value[25] = 0x09;
    ek_ring[0].value[26] = 0x0A;
    ek_ring[0].value[27] = 0x0B;
    ek_ring[0].value[28] = 0x0C;
    ek_ring[0].value[29] = 0x0D;
    ek_ring[0].value[30] = 0x0E;
    ek_ring[0].value[31] = 0x0F;
    ek_ring[0].key_state = KEY_ACTIVE;
    // 1 - 101112131415161718191A1B1C1D1E1F101112131415161718191A1B1C1D1E1F -> ACTIVE
    ek_ring[1].value[0] = 0x10;
    ek_ring[1].value[1] = 0x11;
    ek_ring[1].value[2] = 0x12;
    ek_ring[1].value[3] = 0x13;
    ek_ring[1].value[4] = 0x14;
    ek_ring[1].value[5] = 0x15;
    ek_ring[1].value[6] = 0x16;
    ek_ring[1].value[7] = 0x17;
    ek_ring[1].value[8] = 0x18;
    ek_ring[1].value[9] = 0x19;
    ek_ring[1].value[10] = 0x1A;
    ek_ring[1].value[11] = 0x1B;
    ek_ring[1].value[12] = 0x1C;
    ek_ring[1].value[13] = 0x1D;
    ek_ring[1].value[14] = 0x1E;
    ek_ring[1].value[15] = 0x1F;
    ek_ring[1].value[16] = 0x10;
    ek_ring[1].value[17] = 0x11;
    ek_ring[1].value[18] = 0x12;
    ek_ring[1].value[19] = 0x13;
    ek_ring[1].value[20] = 0x14;
    ek_ring[1].value[21] = 0x15;
    ek_ring[1].value[22] = 0x16;
    ek_ring[1].value[23] = 0x17;
    ek_ring[1].value[24] = 0x18;
    ek_ring[1].value[25] = 0x19;
    ek_ring[1].value[26] = 0x1A;
    ek_ring[1].value[27] = 0x1B;
    ek_ring[1].value[28] = 0x1C;
    ek_ring[1].value[29] = 0x1D;
    ek_ring[1].value[30] = 0x1E;
    ek_ring[1].value[31] = 0x1F;
    ek_ring[1].key_state = KEY_ACTIVE;
    // 2 - 202122232425262728292A2B2C2D2E2F202122232425262728292A2B2C2D2E2F -> ACTIVE
    ek_ring[2].value[0] = 0x20;
    ek_ring[2].value[1] = 0x21;
    ek_ring[2].value[2] = 0x22;
    ek_ring[2].value[3] = 0x23;
    ek_ring[2].value[4] = 0x24;
    ek_ring[2].value[5] = 0x25;
    ek_ring[2].value[6] = 0x26;
    ek_ring[2].value[7] = 0x27;
    ek_ring[2].value[8] = 0x28;
    ek_ring[2].value[9] = 0x29;
    ek_ring[2].value[10] = 0x2A;
    ek_ring[2].value[11] = 0x2B;
    ek_ring[2].value[12] = 0x2C;
    ek_ring[2].value[13] = 0x2D;
    ek_ring[2].value[14] = 0x2E;
    ek_ring[2].value[15] = 0x2F;
    ek_ring[2].value[16] = 0x20;
    ek_ring[2].value[17] = 0x21;
    ek_ring[2].value[18] = 0x22;
    ek_ring[2].value[19] = 0x23;
    ek_ring[2].value[20] = 0x24;
    ek_ring[2].value[21] = 0x25;
    ek_ring[2].value[22] = 0x26;
    ek_ring[2].value[23] = 0x27;
    ek_ring[2].value[24] = 0x28;
    ek_ring[2].value[25] = 0x29;
    ek_ring[2].value[26] = 0x2A;
    ek_ring[2].value[27] = 0x2B;
    ek_ring[2].value[28] = 0x2C;
    ek_ring[2].value[29] = 0x2D;
    ek_ring[2].value[30] = 0x2E;
    ek_ring[2].value[31] = 0x2F;
    ek_ring[2].key_state = KEY_ACTIVE;

    // Session Keys
    // 128 - 0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF -> ACTIVE
    ek_ring[128].value[0] = 0x01;
    ek_ring[128].value[1] = 0x23;
    ek_ring[128].value[2] = 0x45;
    ek_ring[128].value[3] = 0x67;
    ek_ring[128].value[4] = 0x89;
    ek_ring[128].value[5] = 0xAB;
    ek_ring[128].value[6] = 0xCD;
    ek_ring[128].value[7] = 0xEF;
    ek_ring[128].value[8] = 0x01;
    ek_ring[128].value[9] = 0x23;
    ek_ring[128].value[10] = 0x45;
    ek_ring[128].value[11] = 0x67;
    ek_ring[128].value[12] = 0x89;
    ek_ring[128].value[13] = 0xAB;
    ek_ring[128].value[14] = 0xCD;
    ek_ring[128].value[15] = 0xEF;
    ek_ring[128].value[16] = 0x01;
    ek_ring[128].value[17] = 0x23;
    ek_ring[128].value[18] = 0x45;
    ek_ring[128].value[19] = 0x67;
    ek_ring[128].value[20] = 0x89;
    ek_ring[128].value[21] = 0xAB;
    ek_ring[128].value[22] = 0xCD;
    ek_ring[128].value[23] = 0xEF;
    ek_ring[128].value[24] = 0x01;
    ek_ring[128].value[25] = 0x23;
    ek_ring[128].value[26] = 0x45;
    ek_ring[128].value[27] = 0x67;
    ek_ring[128].value[28] = 0x89;
    ek_ring[128].value[29] = 0xAB;
    ek_ring[128].value[30] = 0xCD;
    ek_ring[128].value[31] = 0xEF;
    ek_ring[128].key_state = KEY_ACTIVE;
    // 129 - ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789 -> ACTIVE
    ek_ring[129].value[0] = 0xAB;
    ek_ring[129].value[1] = 0xCD;
    ek_ring[129].value[2] = 0xEF;
    ek_ring[129].value[3] = 0x01;
    ek_ring[129].value[4] = 0x23;
    ek_ring[129].value[5] = 0x45;
    ek_ring[129].value[6] = 0x67;
    ek_ring[129].value[7] = 0x89;
    ek_ring[129].value[8] = 0xAB;
    ek_ring[129].value[9] = 0xCD;
    ek_ring[129].value[10] = 0xEF;
    ek_ring[129].value[11] = 0x01;
    ek_ring[129].value[12] = 0x23;
    ek_ring[129].value[13] = 0x45;
    ek_ring[129].value[14] = 0x67;
    ek_ring[129].value[15] = 0x89;
    ek_ring[129].value[16] = 0xAB;
    ek_ring[129].value[17] = 0xCD;
    ek_ring[129].value[18] = 0xEF;
    ek_ring[129].value[19] = 0x01;
    ek_ring[129].value[20] = 0x23;
    ek_ring[129].value[21] = 0x45;
    ek_ring[129].value[22] = 0x67;
    ek_ring[129].value[23] = 0x89;
    ek_ring[129].value[24] = 0xAB;
    ek_ring[129].value[25] = 0xCD;
    ek_ring[129].value[26] = 0xEF;
    ek_ring[129].value[27] = 0x01;
    ek_ring[129].value[28] = 0x23;
    ek_ring[129].value[29] = 0x45;
    ek_ring[129].value[30] = 0x67;
    ek_ring[129].value[31] = 0x89;
    ek_ring[129].key_state = KEY_ACTIVE;
    // 130 - FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210 -> ACTIVE
    ek_ring[130].value[0] = 0xFE;
    ek_ring[130].value[1] = 0xDC;
    ek_ring[130].value[2] = 0xBA;
    ek_ring[130].value[3] = 0x98;
    ek_ring[130].value[4] = 0x76;
    ek_ring[130].value[5] = 0x54;
    ek_ring[130].value[6] = 0x32;
    ek_ring[130].value[7] = 0x10;
    ek_ring[130].value[8] = 0xFE;
    ek_ring[130].value[9] = 0xDC;
    ek_ring[130].value[10] = 0xBA;
    ek_ring[130].value[11] = 0x98;
    ek_ring[130].value[12] = 0x76;
    ek_ring[130].value[13] = 0x54;
    ek_ring[130].value[14] = 0x32;
    ek_ring[130].value[15] = 0x10;
    ek_ring[130].value[16] = 0xFE;
    ek_ring[130].value[17] = 0xDC;
    ek_ring[130].value[18] = 0xBA;
    ek_ring[130].value[19] = 0x98;
    ek_ring[130].value[20] = 0x76;
    ek_ring[130].value[21] = 0x54;
    ek_ring[130].value[22] = 0x32;
    ek_ring[130].value[23] = 0x10;
    ek_ring[130].value[24] = 0xFE;
    ek_ring[130].value[25] = 0xDC;
    ek_ring[130].value[26] = 0xBA;
    ek_ring[130].value[27] = 0x98;
    ek_ring[130].value[28] = 0x76;
    ek_ring[130].value[29] = 0x54;
    ek_ring[130].value[30] = 0x32;
    ek_ring[130].value[31] = 0x10;
    ek_ring[130].key_state = KEY_ACTIVE;
    // 131 - 9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA -> ACTIVE
    ek_ring[131].value[0] = 0x98;
    ek_ring[131].value[1] = 0x76;
    ek_ring[131].value[2] = 0x54;
    ek_ring[131].value[3] = 0x32;
    ek_ring[131].value[4] = 0x10;
    ek_ring[131].value[5] = 0xFE;
    ek_ring[131].value[6] = 0xDC;
    ek_ring[131].value[7] = 0xBA;
    ek_ring[131].value[8] = 0x98;
    ek_ring[131].value[9] = 0x76;
    ek_ring[131].value[10] = 0x54;
    ek_ring[131].value[11] = 0x32;
    ek_ring[131].value[12] = 0x10;
    ek_ring[131].value[13] = 0xFE;
    ek_ring[131].value[14] = 0xDC;
    ek_ring[131].value[15] = 0xBA;
    ek_ring[131].value[16] = 0x98;
    ek_ring[131].value[17] = 0x76;
    ek_ring[131].value[18] = 0x54;
    ek_ring[131].value[19] = 0x32;
    ek_ring[131].value[20] = 0x10;
    ek_ring[131].value[21] = 0xFE;
    ek_ring[131].value[22] = 0xDC;
    ek_ring[131].value[23] = 0xBA;
    ek_ring[131].value[24] = 0x98;
    ek_ring[131].value[25] = 0x76;
    ek_ring[131].value[26] = 0x54;
    ek_ring[131].value[27] = 0x32;
    ek_ring[131].value[28] = 0x10;
    ek_ring[131].value[29] = 0xFE;
    ek_ring[131].value[30] = 0xDC;
    ek_ring[131].value[31] = 0xBA;
    ek_ring[131].key_state = KEY_ACTIVE;
    // 132 - 0123456789ABCDEFABCDEF01234567890123456789ABCDEFABCDEF0123456789 -> PRE_ACTIVATION
    ek_ring[132].value[0] = 0x01;
    ek_ring[132].value[1] = 0x23;
    ek_ring[132].value[2] = 0x45;
    ek_ring[132].value[3] = 0x67;
    ek_ring[132].value[4] = 0x89;
    ek_ring[132].value[5] = 0xAB;
    ek_ring[132].value[6] = 0xCD;
    ek_ring[132].value[7] = 0xEF;
    ek_ring[132].value[8] = 0xAB;
    ek_ring[132].value[9] = 0xCD;
    ek_ring[132].value[10] = 0xEF;
    ek_ring[132].value[11] = 0x01;
    ek_ring[132].value[12] = 0x23;
    ek_ring[132].value[13] = 0x45;
    ek_ring[132].value[14] = 0x67;
    ek_ring[132].value[15] = 0x89;
    ek_ring[132].value[16] = 0x01;
    ek_ring[132].value[17] = 0x23;
    ek_ring[132].value[18] = 0x45;
    ek_ring[132].value[19] = 0x67;
    ek_ring[132].value[20] = 0x89;
    ek_ring[132].value[21] = 0xAB;
    ek_ring[132].value[22] = 0xCD;
    ek_ring[132].value[23] = 0xEF;
    ek_ring[132].value[24] = 0xAB;
    ek_ring[132].value[25] = 0xCD;
    ek_ring[132].value[26] = 0xEF;
    ek_ring[132].value[27] = 0x01;
    ek_ring[132].value[28] = 0x23;
    ek_ring[132].value[29] = 0x45;
    ek_ring[132].value[30] = 0x67;
    ek_ring[132].value[31] = 0x89;
    ek_ring[132].key_state = KEY_PREACTIVE;
    // 133 - ABCDEF01234567890123456789ABCDEFABCDEF01234567890123456789ABCDEF -> ACTIVE
    ek_ring[133].value[0] = 0xAB;
    ek_ring[133].value[1] = 0xCD;
    ek_ring[133].value[2] = 0xEF;
    ek_ring[133].value[3] = 0x01;
    ek_ring[133].value[4] = 0x23;
    ek_ring[133].value[5] = 0x45;
    ek_ring[133].value[6] = 0x67;
    ek_ring[133].value[7] = 0x89;
    ek_ring[133].value[8] = 0x01;
    ek_ring[133].value[9] = 0x23;
    ek_ring[133].value[10] = 0x45;
    ek_ring[133].value[11] = 0x67;
    ek_ring[133].value[12] = 0x89;
    ek_ring[133].value[13] = 0xAB;
    ek_ring[133].value[14] = 0xCD;
    ek_ring[133].value[15] = 0xEF;
    ek_ring[133].value[16] = 0xAB;
    ek_ring[133].value[17] = 0xCD;
    ek_ring[133].value[18] = 0xEF;
    ek_ring[133].value[19] = 0x01;
    ek_ring[133].value[20] = 0x23;
    ek_ring[133].value[21] = 0x45;
    ek_ring[133].value[22] = 0x67;
    ek_ring[133].value[23] = 0x89;
    ek_ring[133].value[24] = 0x01;
    ek_ring[133].value[25] = 0x23;
    ek_ring[133].value[26] = 0x45;
    ek_ring[133].value[27] = 0x67;
    ek_ring[133].value[28] = 0x89;
    ek_ring[133].value[29] = 0xAB;
    ek_ring[133].value[30] = 0xCD;
    ek_ring[133].value[31] = 0xEF;
    ek_ring[133].key_state = KEY_ACTIVE;
    // 134 - ABCDEF0123456789FEDCBA9876543210ABCDEF0123456789FEDCBA9876543210 -> DEACTIVE
    ek_ring[134].value[0] = 0xAB;
    ek_ring[134].value[1] = 0xCD;
    ek_ring[134].value[2] = 0xEF;
    ek_ring[134].value[3] = 0x01;
    ek_ring[134].value[4] = 0x23;
    ek_ring[134].value[5] = 0x45;
    ek_ring[134].value[6] = 0x67;
    ek_ring[134].value[7] = 0x89;
    ek_ring[134].value[8] = 0xFE;
    ek_ring[134].value[9] = 0xDC;
    ek_ring[134].value[10] = 0xBA;
    ek_ring[134].value[11] = 0x98;
    ek_ring[134].value[12] = 0x76;
    ek_ring[134].value[13] = 0x54;
    ek_ring[134].value[14] = 0x32;
    ek_ring[134].value[15] = 0x10;
    ek_ring[134].value[16] = 0xAB;
    ek_ring[134].value[17] = 0xCD;
    ek_ring[134].value[18] = 0xEF;
    ek_ring[134].value[19] = 0x01;
    ek_ring[134].value[20] = 0x23;
    ek_ring[134].value[21] = 0x45;
    ek_ring[134].value[22] = 0x67;
    ek_ring[134].value[23] = 0x89;
    ek_ring[134].value[24] = 0xFE;
    ek_ring[134].value[25] = 0xDC;
    ek_ring[134].value[26] = 0xBA;
    ek_ring[134].value[27] = 0x98;
    ek_ring[134].value[28] = 0x76;
    ek_ring[134].value[29] = 0x54;
    ek_ring[134].value[30] = 0x32;
    ek_ring[134].value[31] = 0x10;
    ek_ring[134].key_state = KEY_DEACTIVATED;

    // 135 - ABCDEF0123456789FEDCBA9876543210ABCDEF0123456789FEDCBA9876543210 -> DEACTIVE
    ek_ring[135].value[0] = 0x00;
    ek_ring[135].value[1] = 0x00;
    ek_ring[135].value[2] = 0x00;
    ek_ring[135].value[3] = 0x00;
    ek_ring[135].value[4] = 0x00;
    ek_ring[135].value[5] = 0x00;
    ek_ring[135].value[6] = 0x00;
    ek_ring[135].value[7] = 0x00;
    ek_ring[135].value[8] = 0x00;
    ek_ring[135].value[9] = 0x00;
    ek_ring[135].value[10] = 0x00;
    ek_ring[135].value[11] = 0x00;
    ek_ring[135].value[12] = 0x00;
    ek_ring[135].value[13] = 0x00;
    ek_ring[135].value[14] = 0x00;
    ek_ring[135].value[15] = 0x00;
    ek_ring[135].value[16] = 0x00;
    ek_ring[135].value[17] = 0x00;
    ek_ring[135].value[18] = 0x00;
    ek_ring[135].value[19] = 0x00;
    ek_ring[135].value[20] = 0x00;
    ek_ring[135].value[21] = 0x00;
    ek_ring[135].value[22] = 0x00;
    ek_ring[135].value[23] = 0x00;
    ek_ring[135].value[24] = 0x00;
    ek_ring[135].value[25] = 0x00;
    ek_ring[135].value[26] = 0x00;
    ek_ring[135].value[27] = 0x00;
    ek_ring[135].value[28] = 0x00;
    ek_ring[135].value[29] = 0x00;
    ek_ring[135].value[30] = 0x00;
    ek_ring[135].value[31] = 0x00;
    ek_ring[135].key_state = KEY_DEACTIVATED;

    // 136 - ef9f9284cf599eac3b119905a7d18851e7e374cf63aea04358586b0f757670f8
    // Reference:
    // https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Algorithm-Validation-Program/documents/mac/gcmtestvectors.zip
    ek_ring[136].value[0] = 0xff;
    ek_ring[136].value[1] = 0x9f;
    ek_ring[136].value[2] = 0x92;
    ek_ring[136].value[3] = 0x84;
    ek_ring[136].value[4] = 0xcf;
    ek_ring[136].value[5] = 0x59;
    ek_ring[136].value[6] = 0x9e;
    ek_ring[136].value[7] = 0xac;
    ek_ring[136].value[8] = 0x3b;
    ek_ring[136].value[9] = 0x11;
    ek_ring[136].value[10] = 0x99;
    ek_ring[136].value[11] = 0x05;
    ek_ring[136].value[12] = 0xa7;
    ek_ring[136].value[13] = 0xd1;
    ek_ring[136].value[14] = 0x88;
    ek_ring[136].value[15] = 0x51;
    ek_ring[136].value[16] = 0xe7;
    ek_ring[136].value[17] = 0xe3;
    ek_ring[136].value[18] = 0x74;
    ek_ring[136].value[19] = 0xcf;
    ek_ring[136].value[20] = 0x63;
    ek_ring[136].value[21] = 0xae;
    ek_ring[136].value[22] = 0xa0;
    ek_ring[136].value[23] = 0x43;
    ek_ring[136].value[24] = 0x58;
    ek_ring[136].value[25] = 0x58;
    ek_ring[136].value[26] = 0x6b;
    ek_ring[136].value[27] = 0x0f;
    ek_ring[136].value[28] = 0x75;
    ek_ring[136].value[29] = 0x76;
    ek_ring[136].value[30] = 0x70;
    ek_ring[136].value[31] = 0xf9;
    ek_ring[135].key_state = KEY_DEACTIVATED;
}

/**
 * @brief Function: Crypto_Local_Init
 * Initalize TM Frame, CLCW
 **/
void Crypto_Local_Init(void)
{

    // Initialize TM Frame
    // TM Header
    tm_frame.tm_header.tfvn = 0; // Shall be 00 for TM-/TC-SDLP
    tm_frame.tm_header.scid = SCID & 0x3FF;
    tm_frame.tm_header.vcid = 0;
    tm_frame.tm_header.ocff = 1;
    tm_frame.tm_header.mcfc = 1;
    tm_frame.tm_header.vcfc = 1;
    tm_frame.tm_header.tfsh = 0;
    tm_frame.tm_header.sf = 0;
    tm_frame.tm_header.pof = 0;  // Shall be set to 0
    tm_frame.tm_header.slid = 3; // Shall be set to 11
    tm_frame.tm_header.fhp = 0;
    // TM Security Header
    tm_frame.tm_sec_header.spi = 0x0000;
    for (int x = 0; x < IV_SIZE; x++)
    { // Initialization Vector
        *(tm_frame.tm_sec_header.iv + x) = 0x00;
    }
    // TM Payload Data Unit
    for (int x = 0; x < TM_FRAME_DATA_SIZE; x++)
    { // Zero TM PDU
        tm_frame.tm_pdu[x] = 0x00;
    }
    // TM Security Trailer
    for (int x = 0; x < MAC_SIZE; x++)
    { // Zero TM Message Authentication Code
        tm_frame.tm_sec_trailer.mac[x] = 0x00;
    }
    for (int x = 0; x < OCF_SIZE; x++)
    { // Zero TM Operational Control Field
        tm_frame.tm_sec_trailer.ocf[x] = 0x00;
    }
    tm_frame.tm_sec_trailer.fecf = 0xFECF;

    // Initialize CLCW
    clcw.cwt = 0;    // Control Word Type "0"
    clcw.cvn = 0;    // CLCW Version Number "00"
    clcw.sf = 0;     // Status Field
    clcw.cie = 1;    // COP In Effect
    clcw.vci = 0;    // Virtual Channel Identification
    clcw.spare0 = 0; // Reserved Spare
    clcw.nrfa = 0;   // No RF Avaliable Flag
    clcw.nbl = 0;    // No Bit Lock Flag
    clcw.lo = 0;     // Lock-Out Flag
    clcw.wait = 0;   // Wait Flag
    clcw.rt = 0;     // Retransmit Flag
    clcw.fbc = 0;    // FARM-B Counter
    clcw.spare1 = 0; // Reserved Spare
    clcw.rv = 0;     // Report Value

    // Initialize Frame Security Report
    report.cwt = 1;   // Control Word Type "0b1""
    report.vnum = 4;  // FSR Version "0b100""
    report.af = 0;    // Alarm Field
    report.bsnf = 0;  // Bad SN Flag
    report.bmacf = 0; // Bad MAC Flag
    report.ispif = 0; // Invalid SPI Flag
    report.lspiu = 0; // Last SPI Used
    report.snval = 0; // SN Value (LSB)
}

/**
 * @brief Function: Crypto_Calc_CRC_Init_Table
 * Initialize CRC Table
 **/
void Crypto_Calc_CRC_Init_Table(void)
{
    uint16_t val;
    uint32_t poly = 0xEDB88320;
    uint32_t crc;

    // http://create.stephan-brumme.com/crc32/
    for (unsigned int i = 0; i <= 0xFF; i++)
    {
        crc = i;
        for (unsigned int j = 0; j < 8; j++)
        {
            crc = (crc >> 1) ^ (-(int)(crc & 1) & poly);
        }
        crc32Table[i] = crc;
        // printf("crc32Table[%d] = 0x%08x \n", i, crc32Table[i]);
    }

    // Code provided by ESA
    for (int i = 0; i < 256; i++)
    {
        val = 0;
        if ((i & 1) != 0)
            val ^= 0x1021;
        if ((i & 2) != 0)
            val ^= 0x2042;
        if ((i & 4) != 0)
            val ^= 0x4084;
        if ((i & 8) != 0)
            val ^= 0x8108;
        if ((i & 16) != 0)
            val ^= 0x1231;
        if ((i & 32) != 0)
            val ^= 0x2462;
        if ((i & 64) != 0)
            val ^= 0x48C4;
        if ((i & 128) != 0)
            val ^= 0x9188;
        crc16Table[i] = val;
        // printf("crc16Table[%d] = 0x%04x \n", i, crc16Table[i]);
    }
}