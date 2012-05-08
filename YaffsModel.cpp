/*
 * yaffey: Utility for reading, editing and writing YAFFS2 images
 * Copyright (C) 2012 David Place <david.t.place@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <QtGui>

#include "YaffsModel.h"

YaffsModel::YaffsModel(QObject* parent) : QAbstractItemModel(parent) {
    mYaffsRoot = NULL;
    mYaffsSaveControl = NULL;

    mItemsNew = 0;
    mItemsDirty = 0;
    mItemsDeleted = 0;
}

YaffsModel::~YaffsModel() {
    delete mYaffsRoot;
}

void YaffsModel::newImage(const QString& newImageName) {
    mYaffsRoot = YaffsItem::createRoot();
    mItemsNew++;
    mImageFilename = newImageName;

    emit layoutChanged();
}

YaffsReadInfo YaffsModel::openImage(const QString& imageFilename) {
    mImageFilename = imageFilename;

    YaffsReadInfo readInfo;
    memset(&readInfo, 0, sizeof(YaffsReadInfo));

    if (mYaffsRoot == NULL) {
        YaffsControl yaffsControl(mImageFilename.toStdString().c_str(), this);
        if (yaffsControl.open(YaffsControl::OPEN_READ)) {
            readInfo = yaffsControl.readImage();

            mItemsNew = 0;
            mItemsDirty = 0;
            mItemsDeleted = 0;

            emit layoutChanged();
        }
    }

    return readInfo;
}

void YaffsModel::importFile(YaffsItem* parentItem, const QString filenameWithPath) {
    if (parentItem && filenameWithPath.length() > 0) {
        QFileInfo fileInfo(filenameWithPath);
        int filesize = fileInfo.size();

        YaffsItem* importedFile = YaffsItem::createFile(parentItem, filenameWithPath, filesize);
        parentItem->appendChild(importedFile);

        mItemsNew++;
        emit layoutChanged();
    }
}

bool YaffsModel::save() {
    bool saved = false;
/*
    if (isDirty()) {
        if (mItemsNew > 0 || mItemsDeleted > 0) {
            QString originalFilename = mImageFilename;
            QString tmpFilename = mImageFilename + ".tmp";
            saved = saveAs(tmpFilename);
            if (saved) {
                QFile::remove(mImageFilename);
                QFile::rename(tmpFilename, mImageFilename);
                mImageFilename = originalFilename;
            }
        } else {
            YaffsControl yaffsControl(mImageFilename.toStdString().c_str(), NULL);

            if (yaffsControl.open(YaffsControl::OPEN_MODIFY)) {
                QMap<int, YaffsItem*>::const_iterator i;
                for (i = mYaffsObjectsItemMap.begin(); i != mYaffsObjectsItemMap.end(); i++) {
                    YaffsItem* item = i.value();
                    if (item->getCondition() == YaffsItem::DIRTY) {
                        int headerPos = item->getHeaderPosition();
                        const yaffs_obj_hdr& header = item->getHeader();
                        int objectId = item->getObjectId();

                        if (yaffsControl.updateHeader(headerPos, header, objectId)) {
                            item->setCondition(YaffsItem::CLEAN);
                        }
                    }
                }
                saved = true;
            }
        }

        if (saved) {
            mItemsNew = 0;
            mItemsDirty = 0;
            mItemsDeleted = 0;

            QModelIndex root = index(0, 0);
            if (root.isValid()) {
                emit dataChanged(root, root);
            }
        }
    }*/

    return saved;
}

bool YaffsModel::saveAs(const QString& filename) {
    bool result = false;

    if (filename != mImageFilename) {
        mYaffsSaveControl = new YaffsControl(filename.toStdString().c_str(), NULL);
        if (mYaffsSaveControl->open(YaffsControl::OPEN_NEW)) {
            saveDirectory(mYaffsRoot);
            result = true;

            mItemsNew = 0;
            mItemsDirty = 0;
            mItemsDeleted = 0;
        }
        delete mYaffsSaveControl;
        mYaffsSaveControl = NULL;

        if (result) {
            mImageFilename = filename;
        }
    }

    return result;
}

void YaffsModel::saveDirectory(YaffsItem* dirItem) {
    if (dirItem) {
        YaffsItem* parentItem = dirItem->parent();
        if (parentItem) {
            qDebug() << "d: " << dirItem->getFullPath() << ", Parent: " << parentItem->getFullPath();
        } else {
            qDebug() << "d: " << dirItem->getFullPath() << ", Parent: NULL";
        }

        int newObjectId = -1;
        int newHeaderPos = -1;
        if (parentItem) {
            newObjectId = mYaffsSaveControl->addDirectory(dirItem->getHeader(), newHeaderPos);
        } else {
            newObjectId = mYaffsSaveControl->addRoot(dirItem->getHeader(), newHeaderPos);
        }
        dirItem->setHeaderPosition(newHeaderPos);
        dirItem->setObjectId(newObjectId);

        int childCount = dirItem->childCount();
        for (int i = 0; i < childCount; ++i) {
            YaffsItem* childItem = dirItem->child(i);
            childItem->setParentObjectId(newObjectId);

            if (childItem->isDir()) {
                saveDirectory(childItem);
            } else if (childItem->isFile()) {
                saveFile(childItem);
            } else if (childItem->isSimLink()) {
                saveSimLink(childItem);
            }
        }

        dirItem->setCondition(YaffsItem::CLEAN);
    }
}

void YaffsModel::saveFile(YaffsItem* fileItem) {
    if (fileItem) {
        YaffsItem* parentItem = fileItem->parent();
        qDebug() << "f: " << fileItem->getFullPath() << ", Parent: " << parentItem->getFullPath();

        if (fileItem->isFile()) {
            YaffsItem::Condition condition = fileItem->getCondition();
            int filesize = fileItem->getFileSize();
            bool saved = false;
            int newObjectId = -1;
            int newHeaderPos = -1;

            if (condition == YaffsItem::NEW) {
                char* data = new char[filesize];
                QString filename = fileItem->getExternalFilename();
                FILE* file = fopen(filename.toStdString().c_str(), "rb");
                int bytesRead = fread(data, 1, filesize, file);
                if (bytesRead == filesize) {
                    newObjectId = mYaffsSaveControl->addFile(fileItem->getHeader(), newHeaderPos, data, filesize);
                    saved = true;
                }
                delete data;
            } else {
                int headerPosition = fileItem->getHeaderPosition();
                YaffsControl yaffsControl(mImageFilename.toStdString().c_str(), NULL);
                if (yaffsControl.open(YaffsControl::OPEN_READ)) {
                    char* data = yaffsControl.extractFile(headerPosition);
                    if (data != NULL) {
                        newObjectId = mYaffsSaveControl->addFile(fileItem->getHeader(), newHeaderPos, data, filesize);
                        saved = true;
                    }
                }
            }

            if (saved) {
                fileItem->setHeaderPosition(newHeaderPos);
                fileItem->setObjectId(newObjectId);
                fileItem->setCondition(YaffsItem::CLEAN);
            }
        }
    }
}

void YaffsModel::saveSimLink(YaffsItem* simLinkItem) {
    if (simLinkItem) {
        YaffsItem* parentItem = simLinkItem->parent();
        qDebug() << "s: " << simLinkItem->getFullPath() << ", Parent: " << parentItem->getFullPath();
        int newHeaderPos = -1;
        int newObjectId = mYaffsSaveControl->addSimLink(simLinkItem->getHeader(), newHeaderPos);
        simLinkItem->setHeaderPosition(newHeaderPos);
        simLinkItem->setObjectId(newObjectId);
        simLinkItem->setCondition(YaffsItem::CLEAN);
    }
}

QVariant YaffsModel::data(const QModelIndex& itemIndex, int role) const {
    QVariant result = QVariant();
    YaffsItem* item = static_cast<YaffsItem*>(itemIndex.internalPointer());
    if (itemIndex.isValid() && item) {
        if (role == Qt::DisplayRole) {
            if (item == mYaffsRoot && itemIndex.column() == YaffsItem::NAME) {
                result = "/";
            } else {
                result = item->data(itemIndex.column());
            }
        } else if (role == Qt::ForegroundRole) {
            if (itemIndex.column() == YaffsItem::NAME) {
                if (item->isDir()) {
                    result = QVariant(QColor(Qt::blue));
                } else if (item->isFile()) {
                    result = QVariant(QColor(Qt::black));
                } else if (item->isSimLink()) {
                    result = QVariant(QColor(Qt::darkGreen));
                }
            }
        } else if (role == Qt::BackgroundRole) {
//            static const QColor orange(255, 165, 0);

            switch (item->getCondition()) {
            case YaffsItem::CLEAN:
                break;
            case YaffsItem::DIRTY:
//                result = orange;
                break;
            case YaffsItem::NEW:
//                result = Qt::green;
                break;
            case YaffsItem::MOVED:
                break;
            }
        } else if (role == Qt::FontRole) {
            if (itemIndex.column() == YaffsItem::PERMISSIONS) {
                result = QFont("Courier");
            }
        } else if (role == Qt::EditRole) {
            switch (itemIndex.column()) {
            case YaffsItem::NAME:
                result = item->getName();
                break;
            case YaffsItem::PERMISSIONS:
                result = item->getPermissions();
                break;
            case YaffsItem::ALIAS:
                result = item->getAlias();
                break;
            case YaffsItem::USER:
                result = item->getUserId();
                break;
            case YaffsItem::GROUP:
                result = item->getGroupId();
                break;
            }
        }
    }
    return result;
}

bool YaffsModel::setData(const QModelIndex& itemIndex, const QVariant& value, int role) {
    bool result = false;
    if (role == Qt::EditRole) {
        YaffsItem* item = static_cast<YaffsItem*>(itemIndex.internalPointer());
        if (item) {
            switch (itemIndex.column()) {
            case YaffsItem::NAME:
                item->setName(value.toString());
                result = true;
                break;
            case YaffsItem::PERMISSIONS:
                item->setPermissions(value.toUInt());
                result = true;
                break;
            case YaffsItem::ALIAS:
                item->setAlias(value.toString());
                result = true;
                break;
            case YaffsItem::USER:
                item->setUserId(value.toUInt());
                result = true;
                break;
            case YaffsItem::GROUP:
                item->setGroupId(value.toUInt());
                result = true;
                break;
            }
        }
        mItemsDirty += (item->getCondition() == YaffsItem::DIRTY ? 1 : 0);
    }

    if (result) {
        emit dataChanged(itemIndex, itemIndex);
    }

    return result;
}

Qt::ItemFlags YaffsModel::flags(const QModelIndex& itemIndex) const {
    Qt::ItemFlags flags = 0;
    if (itemIndex.isValid()) {
        flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (itemIndex.column() == YaffsItem::NAME) {
            flags |= Qt::ItemIsEditable;
        }
    }
    return flags;
}

QVariant YaffsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            if (section == YaffsItem::NAME) {
                return "Name";
            } else if (section == YaffsItem::SIZE) {
                return "Size";
            } else if (section == YaffsItem::PERMISSIONS) {
                return "Permissions";
            } else if (section == YaffsItem::ALIAS) {
                return "Alias";
            } else if (section == YaffsItem::DATE_ACCESSED) {
                return "Date Accessed";
            } else if (section == YaffsItem::DATE_CREATED) {
                return "Date Created";
            } else if (section == YaffsItem::DATE_MODIFIED) {
                return "Date Modified";
            } else if (section == YaffsItem::USER) {
                return "User";
            } else if (section == YaffsItem::GROUP) {
                return "Group";
            }
#ifdef QT_DEBUG
            else if (section == YaffsItem::OBJECTID) {
                return "ObjectId";
            } else if (section == YaffsItem::PARENTID) {
                return "ParentId";
            } else if (section == YaffsItem::HEADERPOS) {
                return "HeaderPos";
            }
#endif  //QT_DEBUG
        }
    }
    return QVariant();
}

QModelIndex YaffsModel::index(int row, int column, const QModelIndex& parentIndex) const {
    YaffsItem* parent = NULL;

    if (mYaffsRoot && (!parentIndex.isValid() || parentIndex == QModelIndex())) {
//        parent = mYaffsRoot;
        if (row == 0) {
            return createIndex(row, column, mYaffsRoot);
        }
    } else {
        parent = static_cast<YaffsItem*>(parentIndex.internalPointer());
    }

    if (parent) {
        YaffsItem* item = parent->child(row);
        if (item) {
            return createIndex(row, column, item);
        }
    }

    return QModelIndex();
}

QModelIndex YaffsModel::parent(const QModelIndex& itemIndex) const {
    YaffsItem* item = static_cast<YaffsItem*>(itemIndex.internalPointer());
    if (item && item != mYaffsRoot) {
        YaffsItem* parent = item->parent();
        if (parent) {
            return createIndex(parent->row(), 0, parent);
        }
    }
    return QModelIndex();
}

int YaffsModel::rowCount(const QModelIndex& parentIndex) const {
    YaffsItem* parent = NULL;
    int count = 0;

    if (!parentIndex.isValid() || parentIndex == QModelIndex()) {
        if (mYaffsRoot) {
            count = 1;
        }
    } else {
        parent = static_cast<YaffsItem*>(parentIndex.internalPointer());
    }

    if (parent) {
        count = parent->childCount();
    }

    return count;
}

int YaffsModel::columnCount(const QModelIndex& parentIndex) const {
    return YaffsItem::COLUMN_COUNT;
}

bool YaffsModel::removeRows(int row, int count, const QModelIndex& parentIndex) {
    bool result = false;

    YaffsItem* parentItem = static_cast<YaffsItem*>(parentIndex.internalPointer());
    if (parentItem) {
        beginRemoveRows(parentIndex, row, row + (count - 1));
        for (int i = row + (count - 1); i >= row; --i) {
            parentItem->removeChild(row);
            mItemsDeleted++;
        }
        endRemoveRows();
        result = true;
    }

    if (result) {
        emit layoutChanged();
    }
    return result;
}

//from YaffsReaderObserver
void YaffsModel::newItem(int yaffsObjectId, const yaffs_obj_hdr* yaffsObjectHeader, int fileOffset) {
    if (yaffsObjectId == YAFFS_OBJECTID_ROOT) {
        mYaffsRoot = new YaffsItem(NULL, yaffsObjectHeader, fileOffset, yaffsObjectId);
        mYaffsObjectsItemMap.insert(YAFFS_OBJECTID_ROOT, mYaffsRoot);
        return;
    }

    //get childs parent
    YaffsItem* parent = mYaffsObjectsItemMap.value(yaffsObjectHeader->parent_obj_id);

    //create item and map it
    YaffsItem* child = new YaffsItem(parent, yaffsObjectHeader, fileOffset, yaffsObjectId);
    mYaffsObjectsItemMap.insert(yaffsObjectId, child);

    if (parent) {
        //add child to parent
        parent->appendChild(child);
    } else {
        qDebug() << "error, parent not found, id: " << yaffsObjectHeader->parent_obj_id;
        mYaffsObjectsWithoutParent.append(child);
    }
}

void YaffsModel::readComplete() {
    //if image didn't contain a root but did contain other stuff, give model a root
    if (mYaffsRoot == NULL && mYaffsObjectsItemMap.size() > 0) {
        mYaffsRoot = YaffsItem::createRoot();
        mYaffsObjectsItemMap.insert(YAFFS_OBJECTID_ROOT, mYaffsRoot);
    }

    if (mYaffsObjectsWithoutParent.size() > 0) {
        //child objects might have been before parent
        foreach (YaffsItem* child, mYaffsObjectsWithoutParent) {
            YaffsItem* parent = mYaffsObjectsItemMap.value(child->getHeader().parent_obj_id);
            if (parent) {
                parent->appendChild(child);
                qDebug() << "child came before parent in file, parent id: " << child->getHeader().parent_obj_id;
            } else {
                qDebug() << "parent still not found, item name: " << child->getName();
            }
        }
    }
    mYaffsObjectsWithoutParent.clear();
}
