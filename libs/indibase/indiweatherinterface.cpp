/*
    Weather Interface
    Copyright (C) 2018 Jasem Mutlaq (mutlaqja@ikarustech.com)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "indiweatherinterface.h"

#include "indilogger.h"

#include <cstring>

namespace INDI
{

WeatherInterface::WeatherInterface(DefaultDevice *defaultDevice) : m_defaultDevice(defaultDevice)
{
    m_UpdateTimer.callOnTimeout(std::bind(&WeatherInterface::checkWeatherUpdate, this));
    m_UpdateTimer.setSingleShot(true);
    m_UpdateTimer.setInterval(60000);
}

WeatherInterface::~WeatherInterface()
{
    for (int i = 0; i < ParametersNP.nnp; i++)
    {
        free(ParametersN[i].aux0);
        free(ParametersN[i].aux1);
        free(ParametersRangeNP[i].np);
    }

    free(ParametersN);
    free(ParametersRangeNP);
    free(critialParametersL);
}

void WeatherInterface::initProperties(const char *statusGroup, const char *paramsGroup)
{
    m_ParametersGroup = paramsGroup;

    // Update Period
    UpdatePeriodNP[0].fill("PERIOD", "Period (s)", "%.f", 0, 3600, 60, 60);
    UpdatePeriodNP.fill(m_defaultDevice->getDeviceName(), "WEATHER_UPDATE", "Update", statusGroup,
                        IP_RW, 60, IPS_IDLE);

    // Refresh
    RefreshSP[0].fill("REFRESH", "Refresh", ISS_OFF);
    RefreshSP.fill(m_defaultDevice->getDeviceName(), "WEATHER_REFRESH", "Weather", statusGroup, IP_RW, ISR_ATMOST1, 0,
                   IPS_IDLE);

    // Override
    OverrideSP[0].fill("OVERRIDE", "Override Status", ISS_OFF);
    OverrideSP.fill(m_defaultDevice->getDeviceName(), "WEATHER_OVERRIDE", "Safety", statusGroup, IP_RW,
                    ISR_NOFMANY, 0, IPS_IDLE);

    // Parameters
    IUFillNumberVector(&ParametersNP, nullptr, 0, m_defaultDevice->getDeviceName(), "WEATHER_PARAMETERS", "Parameters",
                       paramsGroup, IP_RO, 60, IPS_OK);

    // Weather Status
    IUFillLightVector(&critialParametersLP, nullptr, 0, m_defaultDevice->getDeviceName(), "WEATHER_STATUS", "Status",
                      statusGroup, IPS_IDLE);
}

bool WeatherInterface::updateProperties()
{
    if (m_defaultDevice->isConnected())
    {
        m_defaultDevice->defineProperty(UpdatePeriodNP);
        m_defaultDevice->defineProperty(RefreshSP);
        m_defaultDevice->defineProperty(OverrideSP);

        if (critialParametersL)
            m_defaultDevice->defineProperty(&critialParametersLP);

        if (ParametersN)
            m_defaultDevice->defineProperty(&ParametersNP);

        if (ParametersRangeNP)
        {
            for (int i = 0; i < nRanges; i++)
                m_defaultDevice->defineProperty(&ParametersRangeNP[i]);
        }

        checkWeatherUpdate();
    }
    else
    {
        m_defaultDevice->deleteProperty(UpdatePeriodNP);
        m_defaultDevice->deleteProperty(RefreshSP);
        m_defaultDevice->deleteProperty(OverrideSP);

        if (critialParametersL)
            m_defaultDevice->deleteProperty(critialParametersLP.name);

        if (ParametersN)
            m_defaultDevice->deleteProperty(ParametersNP.name);

        if (ParametersRangeNP)
        {
            for (int i = 0; i < nRanges; i++)
                m_defaultDevice->deleteProperty(ParametersRangeNP[i].name);
        }

    }

    return true;
}

void WeatherInterface::checkWeatherUpdate()
{
    if (!m_defaultDevice->isConnected())
        return;

    IPState state = updateWeather();

    switch (state)
    {
        // Ok
        case IPS_OK:

            if (syncCriticalParameters())
            {
                // Override weather state if required
                if (OverrideSP[0].getState() == ISS_ON)
                    critialParametersLP.s = IPS_OK;

                IDSetLight(&critialParametersLP, nullptr);
            }

            ParametersNP.s = state;
            IDSetNumber(&ParametersNP, nullptr);

            // If update period is set, then set up the timer
            if (UpdatePeriodNP[0].getValue() > 0)
                m_UpdateTimer.start(UpdatePeriodNP[0].getValue() * 1000);

            return;

        // Alert
        // We retry every 5000 ms until we get OK
        case IPS_ALERT:
            ParametersNP.s = state;
            IDSetNumber(&ParametersNP, nullptr);
            break;

        // Weather update is in progress
        default:
            break;
    }

    m_UpdateTimer.start(5000);
}

bool WeatherInterface::processSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    INDI_UNUSED(dev);

    // Refresh
    if (RefreshSP->isNameMatch(name))
    {
        RefreshSP[0].setState(ISS_OFF);
        RefreshSP.setState(IPS_OK);
        RefreshSP->apply();

        checkWeatherUpdate();
        return true;
    }

    // Override
    if (OverrideSP->isNameMatch(name))
    {
        OverrideSP.update(states, names, n);
        if (OverrideSP[0].getState() == ISS_ON)
        {
            DEBUGDEVICE(m_defaultDevice->getDeviceName(), Logger::DBG_WARNING,
                        "Weather override is enabled. Observatory is not safe. Turn off override as soon as possible.");
            OverrideSP.setState(IPS_BUSY);
            critialParametersLP.s = IPS_OK;
            IDSetLight(&critialParametersLP, nullptr);
        }
        else
        {
            DEBUGDEVICE(m_defaultDevice->getDeviceName(), Logger::DBG_SESSION, "Weather override is disabled");
            OverrideSP.setState(IPS_IDLE);

            syncCriticalParameters();
            IDSetLight(&critialParametersLP, nullptr);
        }

        OverrideSP->apply();
        return true;
    }

    return false;
}

bool WeatherInterface::processNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    INDI_UNUSED(dev);

    // Update period
    if (UpdatePeriodNP->isNameMatch(name))
    {
        UpdatePeriodNP.update(values, names, n);
        UpdatePeriodNP->setState(IPS_OK);
        UpdatePeriodNP->apply();

        if (UpdatePeriodNP[0].getValue() == 0)
            DEBUGDEVICE(m_defaultDevice->getDeviceName(), Logger::DBG_SESSION, "Periodic updates are disabled.");
        else
        {
            m_UpdateTimer.setInterval(UpdatePeriodNP[0].getValue() * 1000);
            m_UpdateTimer.start();
        }
        return true;
    }
    else
    {
        // Parameter ranges
        for (int i = 0; i < nRanges; i++)
        {
            if (!strcmp(name, ParametersRangeNP[i].name))
            {
                IUUpdateNumber(&ParametersRangeNP[i], values, names, n);

                ParametersN[i].min               = ParametersRangeNP[i].np[0].value;
                ParametersN[i].max               = ParametersRangeNP[i].np[1].value;
                *(static_cast<double *>(ParametersN[i].aux0)) = ParametersRangeNP[i].np[2].value;

                if (syncCriticalParameters())
                    IDSetLight(&critialParametersLP, nullptr);

                ParametersRangeNP[i].s = IPS_OK;
                IDSetNumber(&ParametersRangeNP[i], nullptr);

                return true;
            }
        }
    }

    return false;
}

IPState WeatherInterface::updateWeather()
{
    DEBUGDEVICE(m_defaultDevice->getDeviceName(), Logger::DBG_ERROR,
                "updateWeather() must be implemented in Weather device child class to update GEOGRAPHIC_COORD properties.");
    return IPS_ALERT;
}

void WeatherInterface::addParameter(std::string name, std::string label, double numMinOk, double numMaxOk,
                                    double percWarning)
{
    DEBUGFDEVICE(m_defaultDevice->getDeviceName(), Logger::DBG_DEBUG, "Parameter %s is added. Ok (%g,%g,%g) ", name.c_str(),
                 numMinOk,
                 numMaxOk, percWarning);

    ParametersN = (ParametersN == nullptr) ? static_cast<INumber *>(malloc(sizeof(INumber))) :
                  static_cast<INumber *>(realloc(ParametersN, (ParametersNP.nnp + 1) * sizeof(INumber)));

    double *warn = static_cast<double *>(malloc(sizeof(double)));

    *warn = percWarning;

    IUFillNumber(&ParametersN[ParametersNP.nnp], name.c_str(), label.c_str(), "%4.2f", numMinOk, numMaxOk, 0, 0);

    ParametersN[ParametersNP.nnp].aux0 = warn;

    ParametersNP.np = ParametersN;
    ParametersNP.nnp++;

    createParameterRange(name, label);
}

void WeatherInterface::setParameterValue(std::string name, double value)
{
    for (int i = 0; i < ParametersNP.nnp; i++)
    {
        if (!strcmp(ParametersN[i].name, name.c_str()))
        {
            ParametersN[i].value = value;
            return;
        }
    }
}

bool WeatherInterface::setCriticalParameter(std::string param)
{
    for (int i = 0; i < ParametersNP.nnp; i++)
    {
        if (!strcmp(ParametersN[i].name, param.c_str()))
        {
            critialParametersL =
                (critialParametersL == nullptr) ?
                static_cast<ILight *>(malloc(sizeof(ILight))) :
                static_cast<ILight *>(realloc(critialParametersL, (critialParametersLP.nlp + 1) * sizeof(ILight)));

            IUFillLight(&critialParametersL[critialParametersLP.nlp], param.c_str(), ParametersN[i].label, IPS_IDLE);

            critialParametersLP.lp = critialParametersL;

            critialParametersLP.nlp++;

            return true;
        }
    }

    DEBUGFDEVICE(m_defaultDevice->getDeviceName(), Logger::DBG_WARNING,
                 "Unable to find parameter %s in list of existing parameters!", param.c_str());
    return false;
}

IPState WeatherInterface::checkParameterState(const INumber &parameter) const
{
    double warn = *(static_cast<double *>(parameter.aux0));
    double rangeWarn = (parameter.max - parameter.min) * (warn / 100);

    if ((parameter.value < parameter.min) || (parameter.value > parameter.max))
    {
        return IPS_ALERT;
    }
    //else if (parameter.value < (parameter.min + rangeWarn) || parameter.value > (parameter.max - rangeWarn))
    // FIXME This is a hack to prevent warnings parameters which minimum values are zero (e.g. Wind)
    else if (   ((parameter.value < (parameter.min + rangeWarn)) && parameter.min != 0)
                || ((parameter.value > (parameter.max - rangeWarn)) && parameter.max != 0))
    {
        return IPS_BUSY;
    }
    else
    {
        return IPS_OK;
    }

    return IPS_IDLE;
}

IPState WeatherInterface::checkParameterState(const std::string &param) const
{
    for (int i = 0; i < ParametersNP.nnp; i++)
    {
        if (!strcmp(ParametersN[i].name, param.c_str()))
        {
            return checkParameterState(ParametersN[i]);
        }
    }

    return IPS_IDLE;
}

bool WeatherInterface::syncCriticalParameters()
{
    if (critialParametersL == nullptr)
        return false;

    std::vector<IPState> preStates(critialParametersLP.nlp);
    for (int i = 0; i < critialParametersLP.nlp; i++)
        preStates[i] = critialParametersL[i].s;

    critialParametersLP.s = IPS_IDLE;

    for (int i = 0; i < critialParametersLP.nlp; i++)
    {
        for (int j = 0; j < ParametersNP.nnp; j++)
        {
            if (!strcmp(critialParametersL[i].name, ParametersN[j].name))
            {
                IPState state = checkParameterState(ParametersN[j]);
                switch (state)
                {
                    case IPS_BUSY:
                        critialParametersL[i].s = IPS_BUSY;
                        DEBUGFDEVICE(m_defaultDevice->getDeviceName(), Logger::DBG_WARNING,
                                     "Warning: Parameter %s value (%g) is in the warning zone!",
                                     ParametersN[j].label, ParametersN[j].value);
                        break;

                    case IPS_ALERT:
                        critialParametersL[i].s = IPS_ALERT;
                        DEBUGFDEVICE(m_defaultDevice->getDeviceName(), Logger::DBG_WARNING,
                                     "Caution: Parameter %s value (%g) is in the danger zone!",
                                     ParametersN[j].label, ParametersN[j].value);
                        break;

                    case IPS_IDLE:
                    case IPS_OK:
                        critialParametersL[i].s = IPS_OK;
                }
            }
        }

        // The overall state is the worst individual state.
        if (critialParametersL[i].s > critialParametersLP.s)
            critialParametersLP.s = critialParametersL[i].s;
    }

    // if Any state changed, return true.
    for (int i = 0; i < critialParametersLP.nlp; i++)
    {
        if (preStates[i] != critialParametersL[i].s)
            return true;
    }

    return false;
}

void WeatherInterface::createParameterRange(std::string name, std::string label)
{
    ParametersRangeNP =
        (ParametersRangeNP == nullptr) ?
        static_cast<INumberVectorProperty *>(malloc(sizeof(INumberVectorProperty))) :
        static_cast<INumberVectorProperty *>(realloc(ParametersRangeNP, (nRanges + 1) * sizeof(INumberVectorProperty)));

    INumber *rangesN = static_cast<INumber *>(malloc(sizeof(INumber) * 3));

    IUFillNumber(&rangesN[0], "MIN_OK", "OK range min", "%4.2f", -1e6, 1e6, 0, ParametersN[nRanges].min);
    IUFillNumber(&rangesN[1], "MAX_OK", "OK range max", "%4.2f", -1e6, 1e6, 0, ParametersN[nRanges].max);
    IUFillNumber(&rangesN[2], "PERC_WARN", "% for Warning", "%g", 0, 100, 1,
                 *(static_cast<double *>(ParametersN[nRanges].aux0)));

    char propName[MAXINDINAME];
    char propLabel[MAXINDILABEL];
    snprintf(propName, MAXINDINAME, "%s", name.c_str());
    snprintf(propLabel, MAXINDILABEL, "%s", label.c_str());

    IUFillNumberVector(&ParametersRangeNP[nRanges], rangesN, 3, m_defaultDevice->getDeviceName(), propName, propLabel,
                       m_ParametersGroup.c_str(),
                       IP_RW, 60, IPS_IDLE);

    nRanges++;
}

bool WeatherInterface::saveConfigItems(FILE *fp)
{
    IUSaveConfigNumber(fp, &UpdatePeriodNP);
    for (int i = 0; i < nRanges; i++)
        IUSaveConfigNumber(fp, &ParametersRangeNP[i]);
    return true;
}


}
