/*
 * PropertyConfiguratorImpl.cpp
 *
 * Copyright 2001, Glen Scott. All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */
#include <log4cpp/Portability.hh>

#ifdef LOG4CPP_HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef LOG4CPP_HAVE_IO_H
#    include <io.h>
#endif
#include <iostream>

#include <string>
#include <fstream>

#include <log4cpp/Category.hh>

// appenders
#include <log4cpp/Appender.hh>
#include <log4cpp/OstreamAppender.hh>
#include <log4cpp/FileAppender.hh>
#include <log4cpp/Win32DebugAppender.hh>
#include <log4cpp/RemoteSyslogAppender.hh>
#ifdef LOG4CPP_HAVE_LIBIDSA
#include <log4cpp/IdsaAppender.hh>
#endif	// LOG4CPP_HAVE_LIBIDSA

// layouts
#include <log4cpp/Layout.hh>
#include <log4cpp/BasicLayout.hh>
#include <log4cpp/SimpleLayout.hh>
#include <log4cpp/PatternLayout.hh>

#include <log4cpp/Priority.hh>
#include <log4cpp/NDC.hh>

#include <list>
#include <vector>

#include "PropertyConfiguratorImpl.hh"
#include "StringUtil.hh"

namespace log4cpp {

    PropertyConfiguratorImpl::PropertyConfiguratorImpl() {
    }

    PropertyConfiguratorImpl::~PropertyConfiguratorImpl() {
    }

    void PropertyConfiguratorImpl::doConfigure(const std::string& initFileName) throw (ConfigureFailure) {
        std::ifstream initFile(initFileName.c_str());

        if (!initFile) {
            throw ConfigureFailure(std::string("File ") + initFileName + " does not exist");
        }

        doConfigure(initFile);
    }


    void PropertyConfiguratorImpl::doConfigure(std::istream& in) throw (ConfigureFailure) {
        // parse the file to get all of the configuration
        _properties.load(in);

        instantiateAllAppenders();
        // get categories
        std::vector<std::string> catList;
        getCategories(catList);

        // add appenders for each category
        for(std::vector<std::string>::iterator iter = catList.begin();
            iter != catList.end(); ++iter) {
            addAppenders(*iter);
        }
    }

    void PropertyConfiguratorImpl::instantiateAllAppenders() throw(ConfigureFailure) {
        std::string currentAppender;

        for(Properties::const_iterator i = _properties.lower_bound("appender.");i != _properties.end(); ++i) {
            const std::string& key = (*i).first;
            const std::string& value = (*i).second;
            std::string::size_type dotIndex = key.find('.');
            if (dotIndex == std::string::npos) {
                // bogus entry, skip
                continue;
            }
            if (key.substr(0, dotIndex) != "appender") {
                // moved past appender properties
                break;
            }
            const string appenderName = key.substr(dotIndex + 1);

            /* WARNING, approaching lame code section:
               skipping of the Appenders properties only to get them 
               again in instantiateAppender.
            */
            if (appenderName == currentAppender) {
                // simply skip properties for the current appender
            } else {
                // a new appender
                currentAppender = appenderName;
                if (appenderName.find('.' != std::string::npos)) {
                    throw ConfigureFailure(std::string("partial appender definition : ") + key);
                }

                _allAppenders[currentAppender] = instantiateAppender(currentAppender);
            }                            
        }
    }

    void PropertyConfiguratorImpl::addAppenders(std::string& categoryName) throw (ConfigureFailure) {
        std::string::size_type length;
        std::string tempCatName;
        std::string leftString, rightString, priority;
        std::list<std::string> tokens;
        Category* category;

        // start by reading the "rootCategory" key
        tempCatName = 
            (categoryName == "rootCategory") ? categoryName : "category." + categoryName;

        Properties::iterator iter = _properties.find(tempCatName);

        if (iter == _properties.end())
            throw ConfigureFailure(std::string("Unable to find category: ") + tempCatName);

        // need to get the root instance of the category
        category = (categoryName == "rootCategory") ?
            &Category::getRoot() : &Category::getInstance(categoryName);

        // if string is not ", appender.." or "" then we only want to set priority ???
        length = (*iter).second.find(",");

        if (length == std::string::npos)
            // something seriously wrong, so bail
            throw ConfigureFailure(std::string("Invalid configuration file: see ") + tempCatName);

        rightString = (*iter).second;
        // store all of the tokens
        do {
            leftString = rightString.substr(0, length);
            rightString = rightString.substr(length + 1);
            tokens.push_back(leftString);
            length = rightString.find(",");
        } while (length != std::string::npos);

        // need to save the last token
        if (rightString.size() > 0)
            tokens.push_back(rightString);

        // made it this far, so we should delete what we have already
        category->removeAllAppenders();

        // loop through the list and either set the priority or add the appender
        std::vector<std::string> v;
        for(std::list<std::string>::const_iterator list_iter = tokens.begin();
            list_iter != tokens.end(); list_iter++) {
            
            StringUtil::split(v, *list_iter, ',');
            std::vector<std::string>::const_iterator i = v.begin();
            if (i == v.end()) {
                // nothing there, strange
                continue;
            }

            std::string priorityName = StringUtil::trim(*i);
            try {
                category->setPriority(Priority::getPriorityValue(priorityName));
            } catch(std::invalid_argument& e) {
                throw ConfigureFailure(std::string("unknown priority '") +
                    priorityName + "' for category '" + categoryName + "'");
            }

            for(++i; i != v.end(); ++i) {
                // not a priority, so it must be an appender
                std::string appenderName = StringUtil::trim(*i);
                AppenderMap::const_iterator appIt = 
                    _allAppenders.find(appenderName);
                if (appIt == _allAppenders.end()) {
                    // appender not found;
                    throw ConfigureFailure(std::string("Appender '") +
                        appenderName + "' not found for category '" + categoryName + "'");
                } else {
                    /* pass by reference, i.e. don't transfer ownership
                     */
                    category->addAppender(*((*appIt).second));
                }
            }
        }
    }

    Appender* PropertyConfiguratorImpl::instantiateAppender(const std::string& appenderName) {
        Appender* appender = NULL;
        std::string appenderPrefix = std::string("appender.") + appenderName;

        // determine the type by the appenderName 
        Properties::iterator key = _properties.find(appenderPrefix);
        if (key == _properties.end())
            throw ConfigureFailure(std::string("Appender '") + appenderName + "' not defined");
		
        std::string::size_type length = (*key).second.find_last_of(".");
        std::string appenderType = (length == std::string::npos) ?
            (*key).second : (*key).second.substr(length+1);

        // and instantiate the appropriate object
        if (appenderType == "ConsoleAppender") {
            appender = new OstreamAppender(appenderName, &std::cout);
        }
        else if (appenderType == "FileAppender") {
            std::string fileName = _properties.getString(appenderPrefix + ".fileName", "foobar");

            appender = new FileAppender(appenderName, fileName);
        }
        else if (appenderType == "SyslogAppender") {
            std::string syslogName = _properties.getString(appenderPrefix + ".syslogName", "syslog");
            std::string syslogHost = _properties.getString(appenderPrefix + ".syslogHost", "localhost");
            int facility = _properties.getInt(appenderPrefix + ".facility", -1);
            int portNumber = _properties.getInt(appenderPrefix + ".portNumber", -1);
            appender = new RemoteSyslogAppender(appenderName, syslogName, 
                                                syslogHost, facility, portNumber);
        }
#ifdef LOG4CPP_HAVE_LIBIDSA
        else if (appenderType == "IdsaAppender") {
            // default idsa name ???
            std::string idsaName = _properties.getString(appenderPrefix + ".idsaName", "foobar");

            appender = new IdsaAppender(appenderName, idsaname);
        }
#endif	// LOG4CPP_HAVE_LIBIDSA

#ifdef WIN32
        // win32 debug appender
        else if (appenderType == "Win32DebugAppender") {
            appender = new Win32DebugAppender(appenderName);
        }
#endif	// WIN32
        else {
            throw ConfigureFailure(std::string("Appender '") + appenderName + 
                                   "' has unknown type '" + appenderType + "'");
        }

        if (appender->requiresLayout()) {
            setLayout(appender, appenderName);
        }

        return appender;
    }

    void PropertyConfiguratorImpl::setLayout(Appender* appender, const std::string& appenderName) {
        // determine the type by appenderName
        std::string tempString;
        Properties::iterator key = 
            _properties.find(std::string("appender.") + appenderName + ".layout");

        if (key == _properties.end())
            throw ConfigureFailure(std::string("Missing layout property for appender '") + 
                                   appenderName + "'");
		
        std::string::size_type length = (*key).second.find_last_of(".");
        std::string layoutType = (length == std::string::npos) ? 
            (*key).second : (*key).second.substr(length+1);
 
        Layout* layout;
        // and instantiate the appropriate object
        if (layoutType == "BasicLayout") {
            layout = new BasicLayout();
        }
        else if (layoutType == "SimpleLayout") {
            layout = new SimpleLayout();
        }
        else if (layoutType == "PatternLayout") {
            // need to read the properties to configure this one
            PatternLayout* patternLayout = new PatternLayout();

            key = _properties.find(std::string("appender.") + appenderName + ".layout.ConversionPattern");
            if (key == _properties.end()) {
                // leave default pattern
            } else {
                // set pattern
                patternLayout->setConversionPattern((*key).second);
            }

            layout = patternLayout;
        }
        else {
            throw ConfigureFailure(std::string("Unknown layout type '" + layoutType +
                                               "' for appender '") + appenderName + "'");
        }

        appender->setLayout(layout);
    }

    /**
     * Get the categories contained within the map of properties.  Since
     * the category looks something like "category.xxxxx.yyy.zzz", we need
     * to search the entire map to figure out which properties are category
     * listings.  Seems like there might be a more elegant solution.
     */
    void PropertyConfiguratorImpl::getCategories(std::vector<std::string>& catlist) throw (ConfigureFailure) {
        // add the root category first
        catlist.push_back(std::string("rootCategory"));

        // then look for "category."
        for (Properties::iterator iter = _properties.begin();
             iter != _properties.end(); iter++) {
            // get the property name and test against "category."
            std::string::size_type length = (*iter).first.find("category.");
            if (length != std::string::npos) {
                // found one, so add it to the list
                std::string catname =
                    (*iter).first.substr(length + std::string("category.").size());
                catlist.push_back(catname);
            }
        }
    }
}
