////////////////////////////////////////////////////////////////////////////////
/// \file CMS5611Pression.cpp
///
/// \brief Fichier du capteur de pression
///
/// \date creation     : 07/03/2024
/// \date modification : 22/02/2025
///

#include "../BertheVario213.h"

MS5611 g_MS5611(0x77);

// Store the current sea level pressure at your location in Pascals.
const float seaLevelPressure = 101325;

////////////////////////////////////////////////////////////////////////////////
/// \brief Determine la difference du jour entre l'altibaro pure et la hauteur
/// sol des fichier hgt
void CVirtCaptPress::SetAltiSolMetres( float AltitudeSolHgt )
{
m_Mutex.PrendreMutex() ;
 m_DiffAltiBaroHauteurSol = AltitudeSolHgt - m_AltitudeBaroPure ;
m_Mutex.RelacherMutex() ;
}

////////////////////////////////////////////////////////////////////////////////
/// \brief En cas de debut de vol sans stabilisation gps, altibaro == alti recalée
void CVirtCaptPress::SetAltiSolUndef()
{
m_Mutex.PrendreMutex() ;
 m_DiffAltiBaroHauteurSol = 0 ;
m_Mutex.RelacherMutex() ;
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Renvoie l'alti pression recalee alti sol a la stabilisation gps
float CVirtCaptPress::GetAltiMetres()
{
m_Mutex.PrendreMutex() ;
 float ret = m_AltitudeBaroPure + m_DiffAltiBaroHauteurSol ;
m_Mutex.RelacherMutex() ;
return ret ;
}

/******************************************************************************/

////////////////////////////////////////////////////////////////////////////////
/// \brief Voir aussi CGlobalVar::InitI2C() pour une frequence I2C qui ne plante pas.
void CMS5611Pression::InitMs5611()
{
//g_GlobalVar.m_MutexI2c.PrendreMutex() ;

#ifdef MS5611_DEBUG
    Serial.println("Initialize MS5611 Sensor");
#endif //MS5611_DEBUG

// init MS5611
if (g_MS5611.begin() == true)
    {
    #ifdef MS5611_DEBUG
    Serial.println("MS5611 found.");
    #endif
    }
else
    {
    //CGlobalVar::BeepError() ;
    #ifdef MS5611_DEBUG
     Serial.println("MS5611 not found. halt.");
    #endif
   }

// meilleur resolution de mb
g_MS5611.setOversampling( OSR_ULTRA_HIGH ) ;
g_MS5611.setCompensation( true ) ;

g_MS5611.setTemperatureOffset( -1.6 ) ;

//g_GlobalVar.m_MutexI2c.RelacherMutex() ;
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Calcul l'altitude en fonction de la pression sans compensation de
/// temperature.
float CMS5611Pression::CalcAltitude(float pressure_mb_x100 , float seaLevelPressure)
{
return (44330.0f * (1.0f - powf((float)pressure_mb_x100 / (float)seaLevelPressure, 0.1902949f)));
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Lecture des info du capteur
void CMS5611Pression::Read()
{
g_MS5611.read() ;
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Renvoie la pression en hPa.
float CMS5611Pression::GetPressionhPa() const
{
return g_MS5611.getPressure() ;
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Renvoie la pression en mb.
float CMS5611Pression::GetPressureMb()
{
return g_MS5611.getPressure() ;
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Renvoie la temperature en degres celsius
float CMS5611Pression::GetTemperatureDegres()
{
return g_MS5611.getTemperature() ;
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Renvoie la mesure d'altitude pression directe du capteur
float CMS5611Pression::GetAltiPressionCapteurMetres()
{
return CalcAltitude( GetPressureMb() * 100. ) ;
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Renvoie l'alti pression filtree recalee alti sol en debut de stabilisation gps
float CMS5611Pression::GetAltiMetres()
{
m_Mutex.PrendreMutex() ;
 float Alti = m_AltitudeBaroPure + m_DiffAltiBaroHauteurSol ;
m_Mutex.RelacherMutex() ;

// pour parer a un probleme
if ( Alti > 9999. || isnan(Alti) || Alti < -500. )
    {
    Alti = 9999. ;
    //SetAltiSolUndef() ; // si probleme de diff alti comme reboot en vol
                          // supprime le 13/01/2025 cause faux depart de vol Vz
    //CGlobalVar::BeepError() ;
    //g_GlobalVar.m_MutexI2c.PrendreMutex() ;
     g_MS5611.reset() ;
    //g_GlobalVar.m_MutexI2c.RelacherMutex() ;
    InitMs5611() ;
    //delay( 500 ) ;
    }
return Alti ;
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Lit l'altitude capteur
void CMS5611Pression::MesureAltitudeCapteur()
{
Read() ;
float AltiMesCapteur = GetAltiPressionCapteurMetres()  ;

m_Mutex.PrendreMutex() ;
 m_AltitudeBaroPure = AltiMesCapteur ;
m_Mutex.RelacherMutex() ;
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Lance la tache de calcul de Vz et acquisition cap magnetique.
void CMS5611Pression::LancerTacheCalculVzCapMag()
{
// tache de calcul Vz
xTaskCreatePinnedToCore(TacheVzCapMag, "MS5611AltiTaskEtMag", VZ_MAG_STACK_SIZE, this, VZ_MAG_PRIORITY,NULL, VZ_MAG_CORE);
}

////////////////////////////////////////////////////////////////////////////////
/// \brief Fonction static qui donne la VZ de la pression/altitude capteur filtree
/// a 3hz.
/// Met a jour aussi le cap magnetique à une frequence plus haute pour ne pas bloquer le
/// capteur.
/// Modifier le fichier ./Projects/BertheVario/.pio/libdeps/esp32dev/MPU9250/MPU9250.h,
/// ligne 85, static constexpr uint8_t MAG_MODE {0x02}; pour une lecture basse frequence
/// du capteur (mais proche de 8hz).
void CMS5611Pression::TacheVzCapMag(void *param)
{
#ifdef G_DEBUG
 Serial.println("tache Vz lancee");
#endif

const int DIV_SECONDES = 3 ;
float AltiPressForVzArr[DIV_SECONDES+1] ;

// init des variables alti pression integree
//g_GlobalVar.m_MutexI2c.PrendreMutex() ;
 g_GlobalVar.m_MS5611Press.MesureAltitudeCapteur() ;
 float AltiBaroFiltree = g_GlobalVar.m_MS5611Press.GetAltiBaroPureMetres() ;
 for ( int i = 0 ; i <= DIV_SECONDES ; i++ )
    AltiPressForVzArr[i] = AltiBaroFiltree ;
//g_GlobalVar.m_MutexI2c.RelacherMutex() ;

// alti pression filtree
while (g_GlobalVar.m_TaskArr[VZ_MAG_NUM_TASK].m_Run)
    {
    //g_GlobalVar.m_MutexI2c.PrendreMutex() ;
     // lecture cap magnetique
     g_GlobalVar.m_QMC5883Mag.LectureCap() ;
    //g_GlobalVar.m_MutexI2c.RelacherMutex() ;

    // 3hz
    delay(1000/DIV_SECONDES) ;

    // mesures du capteur de pression
    //g_GlobalVar.m_MutexI2c.PrendreMutex() ;
     g_GlobalVar.m_MS5611Press.MesureAltitudeCapteur() ;
     AltiBaroFiltree = g_GlobalVar.m_MS5611Press.GetAltiPressionCapteurMetres()  ;
    //g_GlobalVar.m_MutexI2c.RelacherMutex() ;

    // filtrage alti pression
    const float CoefFiltre = g_GlobalVar.m_Config.m_coef_filtre_alti_baro ;
    float AltiPressionFiltree = g_GlobalVar.m_MS5611Press.GetAltiBaroPureMetres() ;
    AltiPressionFiltree = AltiPressionFiltree * CoefFiltre + (1.-CoefFiltre) * AltiBaroFiltree ;
    //Serial.print("altipress:") ;
    //Serial.println(g_GlobalVar.CMS5611::GetAltiBaroPureMetres());

    // decalage du tableau alti fifo par 0 sur 1 secondes
    for ( int i = DIV_SECONDES ; i>0 ; i-- )
        AltiPressForVzArr[ i ] = AltiPressForVzArr[ i - 1 ] ;

    AltiPressForVzArr[ 0 ] = AltiPressionFiltree ;

    // calcul VZ sur 1 secondes
    float VitVert = AltiPressForVzArr[ 0 ] - AltiPressForVzArr[ DIV_SECONDES ] ;
    #ifndef SIMU_VOL
     g_GlobalVar.m_VitVertMS = VitVert ;
    #endif
    }

g_GlobalVar.m_TaskArr[VZ_MAG_NUM_TASK].m_Stopped = true ;
while( true )
    vTaskDelete(NULL) ;
}


